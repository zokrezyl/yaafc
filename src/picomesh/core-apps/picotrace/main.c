/* picotrace - internal trace browser for Picomesh.
 *
 * This is intentionally NOT part of the public Picoforge webapp. It is a
 * generic core operator app that binds loopback by default and talks directly
 * to a mesh backend service, normally trace_collector, over yrpc.
 */

#define _POSIX_C_SOURCE 200809L

#include <picomesh/plugin/trace_collector/trace_collector.h>
#include <picomesh/argv/argv.h>
#include <picomesh/picoclass/rpc.h>
#include <picomesh/core/result.h>
#include <picomesh/core/ytrace.h>
#include <picomesh/json/json.h>
#include <picomesh/loop/loop.h>

#include <picohttpparser.h>

#include <arpa/inet.h>
#include <signal.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define REQ_BUF 65536
#define MAX_HEADERS 64

struct app_config {
    const char *upstream_service;
    const char *upstream_host;
    int upstream_port;
};

/* Vendored ECharts bundle, embedded at build time (see cmake/bin2c.cmake). */
extern const char picotrace_echarts_js[];
extern const unsigned long picotrace_echarts_js_len;

struct buf { char *data; size_t len, cap; };

static void buf_init(struct buf *buf) { buf->data = NULL; buf->len = 0; buf->cap = 0; }
static void buf_free(struct buf *buf) { free(buf->data); buf->data = NULL; buf->len = buf->cap = 0; }

static int buf_reserve(struct buf *buf, size_t extra)
{
    if (buf->data && buf->len + extra + 1 <= buf->cap) return 1;
    size_t new_cap = buf->cap ? buf->cap : 4096;
    while (buf->len + extra + 1 > new_cap) new_cap *= 2;
    char *new_data = realloc(buf->data, new_cap);
    if (!new_data) return 0;
    buf->data = new_data; buf->cap = new_cap;
    return 1;
}

static void buf_putn(struct buf *buf, const char *str, size_t len)
{
    if (!buf_reserve(buf, len)) return;
    memcpy(buf->data + buf->len, str, len);
    buf->len += len;
    buf->data[buf->len] = 0;
}

static void buf_puts(struct buf *buf, const char *str) { buf_putn(buf, str, strlen(str)); }

static void buf_printf(struct buf *buf, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char tmp[2048];
    int len = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (len < 0) return;
    if ((size_t)len < sizeof(tmp)) { buf_putn(buf, tmp, (size_t)len); return; }
    char *big = malloc((size_t)len + 1);
    if (!big) return;
    va_start(ap, fmt);
    vsnprintf(big, (size_t)len + 1, fmt, ap);
    va_end(ap);
    buf_putn(buf, big, (size_t)len);
    free(big);
}

static void buf_esc(struct buf *buf, const char *str)
{
    for (const char *p = str ? str : ""; *p; ++p) {
        switch (*p) {
        case '&':  buf_puts(buf, "&amp;");  break;
        case '<':  buf_puts(buf, "&lt;");   break;
        case '>':  buf_puts(buf, "&gt;");   break;
        case '"': buf_puts(buf, "&quot;"); break;
        case '\'': buf_puts(buf, "&#39;");  break;
        default:   buf_putn(buf, p, 1);      break;
        }
    }
}

static void fmt_duration(char *out, size_t cap, uint64_t ns)
{
    if (ns >= 1000000000ull)
        snprintf(out, cap, "%.2fs", (double)ns / 1000000000.0);
    else if (ns >= 1000000ull)
        snprintf(out, cap, "%.2fms", (double)ns / 1000000.0);
    else if (ns >= 1000ull)
        snprintf(out, cap, "%.2fus", (double)ns / 1000.0);
    else
        snprintf(out, cap, "%lluns", (unsigned long long)ns);
}

static char *query_get(const char *path, const char *key)
{
    const char *cursor = strchr(path, '?');
    if (!cursor) return NULL;
    cursor++;
    size_t key_len = strlen(key);
    while (*cursor) {
        const char *amp = strchr(cursor, '&');
        const char *end = amp ? amp : cursor + strlen(cursor);
        const char *eq = memchr(cursor, '=', (size_t)(end - cursor));
        if (eq && (size_t)(eq - cursor) == key_len && memcmp(cursor, key, key_len) == 0) {
            size_t len = (size_t)(end - eq - 1);
            char *out = malloc(len + 1);
            if (!out) return NULL;
            memcpy(out, eq + 1, len);
            out[len] = 0;
            for (char *p = out; *p; ++p) if (*p == '+') *p = ' ';
            return out;
        }
        if (!amp) break;
        cursor = amp + 1;
    }
    return NULL;
}

static const char *reason(int status)
{
    switch (status) {
    case 200: return "OK";
    case 404: return "Not Found";
    case 500: return "Internal Server Error";
    default: return "OK";
    }
}

static void send_response(struct loop_stream *stream, int status, const char *ctype,
                          const char *body, size_t body_len)
{
    char header[512];
    int header_len = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",
        status, reason(status), ctype, body_len);
    if (header_len > 0) loop_write(stream, header, (size_t)header_len);
    if (body && body_len) loop_write(stream, body, body_len);
}

static int collector_connect(const char *host, int port)
{
    if (!host || !*host) host = "127.0.0.1";
    const char *dial = (strcmp(host, "0.0.0.0") == 0) ? "127.0.0.1" : host;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, dial, &addr.sin_addr) != 1) { close(fd); return -1; }
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) { close(fd); return -1; }
    return fd;
}

struct collector_session {
    struct peer_channel *channel;
    struct object *obj;
    struct ctx ctx;
};

static struct picomesh_int_result collector_open(const struct app_config *cfg, struct collector_session *session)
{
    memset(session, 0, sizeof(*session));
    if (!cfg || cfg->upstream_port <= 0) return PICOMESH_OK(picomesh_int, 0);
    int fd = collector_connect(cfg->upstream_host, cfg->upstream_port);
    if (fd < 0) return PICOMESH_OK(picomesh_int, 0);
    session->channel = peer_channel_create(fd);
    if (!session->channel) { close(fd); return PICOMESH_OK(picomesh_int, 0); }
    session->ctx = (struct ctx){.peer = session->channel};
    struct object_ptr_result obj_r = trace_collector_trace_collector_create(&session->ctx);
    if (PICOMESH_IS_ERR(obj_r)) {
        peer_channel_destroy(session->channel);
        session->channel = NULL;
        return PICOMESH_ERR(picomesh_int, "collector_open: collector create failed", obj_r);
    }
    session->obj = obj_r.value;
    return PICOMESH_OK(picomesh_int, 1);
}

static void collector_close(struct collector_session *session)
{
    if (!session || !session->channel) return;
    object_release_in_ctx(&session->ctx, session->obj);
    peer_channel_destroy(session->channel);
    session->channel = NULL;
    session->obj = NULL;
}

static struct picomesh_string_result collector_get_trace(const struct app_config *cfg, const char *trace_id)
{
    struct collector_session session;
    struct picomesh_int_result open_res = collector_open(cfg, &session);
    PICOMESH_RETURN_IF_ERR(picomesh_string, open_res, "collector_get_trace: open failed");
    if (!open_res.value) return PICOMESH_OK(picomesh_string, NULL);
    struct picomesh_string_result get_trace_res =
        trace_collector_trace_collector_get_trace(&session.ctx, session.obj, NULL, trace_id ? trace_id : "");
    collector_close(&session);
    PICOMESH_RETURN_IF_ERR(picomesh_string, get_trace_res, "collector_get_trace: query failed");
    return get_trace_res;
}

static struct picomesh_string_result collector_traces(const struct app_config *cfg, const char *service,
                              const char *status, uint32_t since_secs)
{
    struct collector_session session;
    struct picomesh_int_result open_res = collector_open(cfg, &session);
    PICOMESH_RETURN_IF_ERR(picomesh_string, open_res, "collector_traces: open failed");
    if (!open_res.value) return PICOMESH_OK(picomesh_string, NULL);
    struct picomesh_string_result traces_res =
        trace_collector_trace_collector_traces(&session.ctx, session.obj, NULL,
                                               service ? service : "", status ? status : "", since_secs);
    collector_close(&session);
    PICOMESH_RETURN_IF_ERR(picomesh_string, traces_res, "collector_traces: query failed");
    return traces_res;
}

static struct picomesh_string_result collector_services(const struct app_config *cfg)
{
    struct collector_session session;
    struct picomesh_int_result open_res = collector_open(cfg, &session);
    PICOMESH_RETURN_IF_ERR(picomesh_string, open_res, "collector_services: open failed");
    if (!open_res.value) return PICOMESH_OK(picomesh_string, NULL);
    struct picomesh_string_result services_res = trace_collector_trace_collector_services(&session.ctx, session.obj, NULL);
    collector_close(&session);
    PICOMESH_RETURN_IF_ERR(picomesh_string, services_res, "collector_services: query failed");
    return services_res;
}

static struct picomesh_string_result collector_stats(const struct app_config *cfg)
{
    struct collector_session session;
    struct picomesh_int_result open_res = collector_open(cfg, &session);
    PICOMESH_RETURN_IF_ERR(picomesh_string, open_res, "collector_stats: open failed");
    if (!open_res.value) return PICOMESH_OK(picomesh_string, NULL);
    struct picomesh_string_result stats_res = trace_collector_trace_collector_stats(&session.ctx, session.obj, NULL);
    collector_close(&session);
    PICOMESH_RETURN_IF_ERR(picomesh_string, stats_res, "collector_stats: query failed");
    return stats_res;
}

/* Embed a JSON document inside a <script> block. Only '<' has to be
 * neutralized so a literal "</script>" appearing in string data cannot close
 * the element early; escaping it as < keeps the document valid JSON. */
static void buf_put_json_script(struct buf *buf, const char *json)
{
    for (const char *p = json; *p; ++p) {
        if (*p == '<') buf_puts(buf, "\\u003c");
        else buf_putn(buf, p, 1);
    }
}

static void render_head(struct buf *buf)
{
    buf_puts(buf,
        "<!doctype html><html><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>picotrace</title>"
        "<script src=\"/echarts.min.js\"></script>"
        "<style>"
        ":root{--bg:#f7f8fa;--fg:#1f2328;--muted:#59636e;--border:#d0d7de;--panel:#fff;--sub:#f3f5f7;--accent:#0969da;--danger:#cf222e;--ok:#1a7f37}"
        "*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--fg);font:14px/1.5 -apple-system,BlinkMacSystemFont,Segoe UI,Helvetica,Arial,sans-serif}"
        "header{height:48px;background:#1f2530;color:#e6e9ee;display:flex;align-items:center;padding:0 16px;gap:16px}header strong{font-size:16px}"
        "main{max-width:1280px;margin:0 auto;padding:20px}a{color:var(--accent);text-decoration:none}a:hover{text-decoration:underline}"
        ".panel{background:var(--panel);border:1px solid var(--border);border-radius:6px;margin:0 0 14px;overflow:hidden}.panel h2{font-size:14px;margin:0;padding:10px 14px;background:var(--sub);border-bottom:1px solid var(--border)}.panel-body{padding:14px}"
        "form{display:flex;gap:10px;align-items:flex-end;flex-wrap:wrap}label{display:block;color:var(--muted)}input,select{display:block;margin-top:3px;padding:6px 8px;border:1px solid var(--border);border-radius:6px;background:white}button{padding:7px 12px;border:1px solid var(--accent);border-radius:6px;background:var(--accent);color:white}"
        "table{width:100%;border-collapse:collapse;font-size:13px;table-layout:fixed}th,td{padding:7px 10px;border-bottom:1px solid var(--border);text-align:left;vertical-align:middle}th{background:var(--sub);color:var(--muted);font-size:11px;text-transform:uppercase;letter-spacing:.03em}code,.mono{font-family:ui-monospace,SFMono-Regular,Consolas,monospace;overflow-wrap:anywhere}"
        ".badge{display:inline-block;border:1px solid var(--border);border-radius:999px;padding:1px 7px;font-size:12px}.ok{color:var(--ok);background:#dafbe1;border-color:#b5e6c6}.err{color:var(--danger);background:#ffebe9;border-color:#ffcecb}.muted{color:var(--muted)}"
        "#trace-chart{width:100%;min-height:200px}"
        ".crumb{margin:2px 0 14px;font-size:13px}.trace-row{cursor:pointer}.trace-row:hover{background:#f0f6ff}"
        "</style></head><body><header><strong>picotrace</strong><span class=\"muted\">internal Picomesh trace browser</span></header><main>");
}

static struct picomesh_void_result render_trace_detail(struct buf *buf, const struct app_config *cfg, const char *trace_id)
{
    if (!trace_id || !*trace_id) return PICOMESH_OK_VOID();
    struct picomesh_string_result trace_res = collector_get_trace(cfg, trace_id);
    char *json = PICOMESH_IS_OK(trace_res) ? trace_res.value : NULL;
    if (PICOMESH_IS_ERR(trace_res)) { picomesh_error_print(stderr, "picotrace: collector query", trace_res.error); picomesh_error_destroy(trace_res.error); }
    if (!json) {
        buf_puts(buf, "<section class=\"panel\"><h2>Trace detail</h2><div class=\"panel-body\"><p class=\"err\">Cannot load trace.</p></div></section>");
        return PICOMESH_OK_VOID();
    }
    /* The server does no span parsing or layout: it hands the raw collector
     * JSON to the browser and lets Apache ECharts build the waterfall
     * client-side. */
    buf_puts(buf, "<section class=\"panel\"><h2>Trace detail</h2><div class=\"panel-body\"><p><code>");
    buf_esc(buf, trace_id);
    buf_puts(buf, "</code> <span id=\"trace-meta\" class=\"muted\"></span></p>");
    buf_puts(buf, "<div id=\"trace-chart\"></div>");
    buf_puts(buf, "<script type=\"application/json\" id=\"trace-data\">");
    buf_put_json_script(buf, json);
    buf_puts(buf, "</script>");
    buf_puts(buf, "<script>");
    buf_puts(buf,
        "(function(){\n"
        "var el=document.getElementById('trace-data');\n"
        "var container=document.getElementById('trace-chart');\n"
        "if(!el||!container)return;\n"
        "if(typeof echarts==='undefined'){container.innerHTML='<p class=err>ECharts failed to load (needs network access for the CDN).</p>';return;}\n"
        "var trace=JSON.parse(el.textContent);\n"
        "var spans=(trace&&trace.spans)||[];\n"
        "if(!spans.length){container.innerHTML='<p class=muted>No spans found.</p>';return;}\n"
        "var byId={};for(var i=0;i<spans.length;i++)byId[spans[i].span_id]=spans[i];\n"
        "var minStart=null,maxEnd=0;\n"
        "for(var i=0;i<spans.length;i++){var st=Number(spans[i].start_time_ns||0),d=Number(spans[i].duration_ns||0);if(minStart===null||st<minStart)minStart=st;if(st+d>maxEnd)maxEnd=st+d;}\n"
        "if(minStart===null)minStart=0;\n"
        "var total=maxEnd>minStart?maxEnd-minStart:1;\n"
        "var children={},roots=[];\n"
        "for(var i=0;i<spans.length;i++){var s=spans[i],p=s.parent_span_id;if(p&&byId[p]&&p!==s.span_id){(children[p]=children[p]||[]).push(s);}else{roots.push(s);}}\n"
        "function byStart(a,b){return Number(a.start_time_ns||0)-Number(b.start_time_ns||0);}\n"
        "var ordered=[],depthOf={};\n"
        "(function walk(list,depth){list.sort(byStart);for(var i=0;i<list.length;i++){var s=list[i];ordered.push(s);depthOf[s.span_id]=depth;if(children[s.span_id])walk(children[s.span_id],depth+1);}})(roots,0);\n"
        "var palette=['#0072c3','#eb6200','#8a3ffc','#b28600','#005d5d','#fa4d56','#198038','#9f1853','#002d9c','#6f6f6f','#00539c','#8a3800','#6929c4','#8e6a00','#002d2d','#570408','#0e6027','#510224','#001141','#491d8b'];\n"
        "var svcIdx={},svcN=0;\n"
        "function colorFor(svc){if(!(svc in svcIdx))svcIdx[svc]=svcN++;return palette[svcIdx[svc]%palette.length];}\n"
        "function fmtDur(ns){ns=Number(ns)||0;if(ns<1000)return ns+'ns';if(ns<1e6)return (ns/1e3).toFixed(1)+'\xc2\xb5s';if(ns<1e9)return (ns/1e6).toFixed(2)+'ms';return (ns/1e9).toFixed(2)+'s';}\n"
        "function esc(s){return String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;');}\n"
        "var categories=[],data=[];\n"
        "for(var r=0;r<ordered.length;r++){var s=ordered[r],st=Number(s.start_time_ns||0)-minStart,d=Number(s.duration_ns||0),depth=depthOf[s.span_id]||0;var pad='';for(var k=0;k<depth;k++)pad+='\\u00a0\\u00a0';categories.push(pad+(s.name||'(unnamed)'));data.push({value:[r,st,st+d,s.status==='error'?1:0],itemStyle:{color:colorFor(s.service_name||'')},span:s});}\n"
        "function renderItem(params,api){var row=api.value(0);var start=api.coord([api.value(1),row]);var end=api.coord([api.value(2),row]);var h=api.size([0,1])[1]*0.6;var w=end[0]-start[0];if(w<1)w=1;var cs=params.coordSys;var x=Math.max(start[0],cs.x);var x2=Math.min(start[0]+w,cs.x+cs.width);if(x2<=x)return null;var style=api.style();if(api.value(3))style=Object.assign({},style,{stroke:'#cf222e',lineWidth:1});return{type:'rect',shape:{x:x,y:start[1]-h/2,width:x2-x,height:h,r:2},style:style};}\n"
        "var meta=document.getElementById('trace-meta');if(meta)meta.textContent=fmtDur(total)+' \xc2\xb7 '+spans.length+' spans';\n"
        "container.style.height=Math.max(220,ordered.length*24+90)+'px';\n"
        "var chart=echarts.init(container);\n"
        "chart.setOption({animation:false,grid:{left:270,right:24,top:20,bottom:54},"
        "tooltip:{trigger:'item',formatter:function(p){var s=p.data.span;return '<b>'+esc(s.name||'')+'</b><br/>'+(s.service_name?esc(s.service_name)+'<br/>':'')+'duration '+fmtDur(s.duration_ns)+'<br/>start +'+fmtDur(Number(s.start_time_ns||0)-minStart)+(s.status==='error'?'<br/><span style=\\\"color:#cf222e\\\">error</span>':'');}},"
        "xAxis:{type:'value',min:0,max:total,axisLabel:{formatter:function(v){return fmtDur(v);}},splitLine:{show:true,lineStyle:{color:'#eef1f4'}}},"
        "yAxis:{type:'category',data:categories,inverse:true,axisTick:{show:false},axisLine:{show:false},axisLabel:{interval:0,color:'#1f2328',fontSize:12,width:230,overflow:'truncate',align:'left',margin:236}},"
        "dataZoom:[{type:'inside',xAxisIndex:0,filterMode:'none'},{type:'slider',xAxisIndex:0,filterMode:'none',height:20,bottom:14,labelFormatter:function(v){return fmtDur(v);}}],"
        "series:[{type:'custom',renderItem:renderItem,encode:{x:[1,2],y:0},data:data}]});\n"
        "window.addEventListener('resize',function(){chart.resize();});\n"
        "})();\n");
    buf_puts(buf, "</script></div></section>");
    free(json);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result render_page(struct loop_stream *stream, struct loop *loop, const struct app_config *cfg,
                        const char *full_path)
{
    (void)loop;
    char *service = query_get(full_path, "service");
    char *status = query_get(full_path, "status");
    char *since_s = query_get(full_path, "since");
    char *trace = query_get(full_path, "trace");
    long since = since_s && *since_s ? atol(since_s) : 3600;
    if (since < 0) since = 0;

    struct buf buf; buf_init(&buf);
    render_head(&buf);

    if (trace && *trace) {
        /* Dedicated trace view (Jaeger-style): the waterfall is the whole
         * page, reached by clicking a row in the results list. A breadcrumb
         * returns to the search list — no results table underneath. */
        buf_puts(&buf, "<p class=\"crumb\"><a href=\"/\">&larr; Search</a></p>");
        struct picomesh_void_result detail_res = render_trace_detail(&buf, cfg, trace);
        if (PICOMESH_IS_ERR(detail_res)) {
            picomesh_error_print(stderr, "picotrace: render_trace_detail", detail_res.error);
            picomesh_error_destroy(detail_res.error);
        }
    } else {
        /* Search / results view: filter form, collector stats, results list. */
        struct picomesh_string_result traces_res = collector_traces(cfg, service ? service : "", status ? status : "", (uint32_t)since);
        char *traces_json = PICOMESH_IS_OK(traces_res) ? traces_res.value : NULL;
        if (PICOMESH_IS_ERR(traces_res)) { picomesh_error_print(stderr, "picotrace: collector query", traces_res.error); picomesh_error_destroy(traces_res.error); }
        struct picomesh_string_result services_res = collector_services(cfg);
        char *services_json = PICOMESH_IS_OK(services_res) ? services_res.value : NULL;
        if (PICOMESH_IS_ERR(services_res)) { picomesh_error_print(stderr, "picotrace: collector query", services_res.error); picomesh_error_destroy(services_res.error); }
        struct picomesh_string_result stats_res = collector_stats(cfg);
        char *stats_json = PICOMESH_IS_OK(stats_res) ? stats_res.value : NULL;
        if (PICOMESH_IS_ERR(stats_res)) { picomesh_error_print(stderr, "picotrace: collector query", stats_res.error); picomesh_error_destroy(stats_res.error); }

        buf_puts(&buf, "<section class=\"panel\"><h2>Search</h2><div class=\"panel-body\"><form method=\"get\" action=\"/\">");
        buf_puts(&buf, "<label>Service<select name=\"service\"><option value=\"\">All services</option>");
        struct json_doc *svc_doc = services_json ? json_parse(services_json, strlen(services_json)) : NULL;
        const struct json_value *svc_arr = svc_doc ? json_object_get(json_doc_root(svc_doc), "services") : NULL;
        size_t svc_count = svc_arr ? json_array_size(svc_arr) : 0;
        for (size_t i = 0; i < svc_count; ++i) {
            const struct json_value *entry = json_array_at(svc_arr, i);
            const char *name = json_as_string(json_object_get(entry, "service_name"), "");
            buf_puts(&buf, "<option value=\""); buf_esc(&buf, name); buf_puts(&buf, "\"");
            if (service && strcmp(service, name) == 0) buf_puts(&buf, " selected");
            buf_puts(&buf, ">"); buf_esc(&buf, name); buf_puts(&buf, "</option>");
        }
        buf_puts(&buf, "</select></label><label>Status<select name=\"status\">");
        buf_printf(&buf, "<option value=\"\"%s>Any</option>", (!status || !*status) ? " selected" : "");
        buf_printf(&buf, "<option value=\"ok\"%s>OK</option>", (status && strcmp(status, "ok") == 0) ? " selected" : "");
        buf_printf(&buf, "<option value=\"error\"%s>Error</option>", (status && strcmp(status, "error") == 0) ? " selected" : "");
        buf_puts(&buf, "</select></label><label>Lookback<select name=\"since\">");
        const long windows[] = {300, 3600, 21600, 86400, 0};
        const char *labels[] = {"5 minutes", "1 hour", "6 hours", "24 hours", "All"};
        for (size_t i = 0; i < sizeof(windows) / sizeof(windows[0]); ++i)
            buf_printf(&buf, "<option value=\"%ld\"%s>%s</option>", windows[i], since == windows[i] ? " selected" : "", labels[i]);
        buf_puts(&buf, "</select></label><label>Trace ID<input name=\"trace\" value=\"\"></label><button type=\"submit\">Search</button></form></div></section>");

        if (stats_json) {
            struct json_doc *doc = json_parse(stats_json, strlen(stats_json));
            if (doc) {
                const struct json_value *stats_root = json_doc_root(doc);
                buf_puts(&buf, "<section class=\"panel\"><h2>Collector</h2><div class=\"panel-body\"><span class=\"muted\">stored</span> ");
                buf_printf(&buf, "%lld", (long long)json_as_int(json_object_get(stats_root, "stored"), 0));
                buf_puts(&buf, " &nbsp; <span class=\"muted\">ingested</span> ");
                buf_printf(&buf, "%lld", (long long)json_as_int(json_object_get(stats_root, "ingested"), 0));
                buf_puts(&buf, " &nbsp; <span class=\"muted\">evicted</span> ");
                buf_printf(&buf, "%lld", (long long)json_as_int(json_object_get(stats_root, "evicted"), 0));
                buf_puts(&buf, "</div></section>");
                json_doc_free(doc);
            }
        }

        buf_puts(&buf, "<section class=\"panel\"><h2>Recent traces</h2><div class=\"panel-body\">");
        if (!traces_json) {
            buf_puts(&buf, "<p class=\"err\">Cannot query trace collector through the configured upstream.</p>");
        } else {
            struct json_doc *doc = json_parse(traces_json, strlen(traces_json));
            const struct json_value *arr = doc ? json_object_get(json_doc_root(doc), "traces") : NULL;
            size_t trace_count = arr ? json_array_size(arr) : 0;
            if (!trace_count) buf_puts(&buf, "<p class=\"muted\">No traces match.</p>");
            else {
                buf_puts(&buf, "<table><thead><tr><th>Trace</th><th>Root</th><th>Service</th><th>Duration</th><th>Spans</th><th>Status</th></tr></thead><tbody>");
                for (size_t i = 0; i < trace_count; ++i) {
                    const struct json_value *trace_entry = json_array_at(arr, i);
                    const char *trace_id = json_as_string(json_object_get(trace_entry, "trace_id"), "");
                    const char *root = json_as_string(json_object_get(trace_entry, "root_name"), "");
                    const char *service_name = json_as_string(json_object_get(trace_entry, "service_name"), "");
                    const char *status_str = json_as_string(json_object_get(trace_entry, "status"), "ok");
                    uint64_t duration_ns = (uint64_t)json_as_int(json_object_get(trace_entry, "duration_ns"), 0);
                    char dur[32]; fmt_duration(dur, sizeof(dur), duration_ns);
                    /* Whole row links to the dedicated trace view. */
                    buf_puts(&buf, "<tr class=\"trace-row\" onclick=\"location='/?trace="); buf_esc(&buf, trace_id);
                    buf_puts(&buf, "'\"><td><a class=\"mono\" href=\"/?trace="); buf_esc(&buf, trace_id); buf_puts(&buf, "\">"); buf_esc(&buf, trace_id); buf_puts(&buf, "</a></td><td>");
                    buf_esc(&buf, root); buf_puts(&buf, "</td><td>"); buf_esc(&buf, service_name); buf_puts(&buf, "</td><td>"); buf_esc(&buf, dur); buf_puts(&buf, "</td><td>");
                    buf_printf(&buf, "%lld", (long long)json_as_int(json_object_get(trace_entry, "span_count"), 0));
                    buf_puts(&buf, "</td><td><span class=\"badge "); buf_puts(&buf, strcmp(status_str, "error") == 0 ? "err" : "ok"); buf_puts(&buf, "\">"); buf_esc(&buf, status_str); buf_puts(&buf, "</span></td></tr>");
                }
                buf_puts(&buf, "</tbody></table>");
            }
            if (doc) json_doc_free(doc);
        }
        buf_puts(&buf, "</div></section>");

        if (svc_doc) json_doc_free(svc_doc);
        free(traces_json); free(services_json); free(stats_json);
    }

    buf_puts(&buf, "</main></body></html>");
    send_response(stream, 200, "text/html; charset=utf-8", buf.data ? buf.data : "", buf.len);
    buf_free(&buf);

    free(service); free(status); free(since_s); free(trace);
    return PICOMESH_OK_VOID();
}

/* Per-connection serve coroutine — its void signature is fixed by the loop's
 * accept-handler API; a page-render failure is absorbed here (chain rendered). */
PICOMESH_EXTERNAL_CALLBACK
static void serve_one(struct loop *loop, struct loop_stream *stream, void *user_data)
{
    const struct app_config *cfg = user_data;
    char *buf = malloc(REQ_BUF);
    if (!buf) { loop_close(stream); return; }
    size_t total = 0, last = 0;
    const char *method = NULL, *path = NULL;
    size_t method_len = 0, path_len = 0;
    int minor = 0;
    struct phr_header headers[MAX_HEADERS];
    size_t header_count = 0;
    int parsed = -1;
    while (total < REQ_BUF) {
        struct picomesh_size_result read_res = loop_read_some(stream, buf + total, REQ_BUF - total);
        if (PICOMESH_IS_ERR(read_res)) { picomesh_error_destroy(read_res.error); free(buf); loop_close(stream); return; }
        if (read_res.value == 0) { free(buf); loop_close(stream); return; } /* clean EOF */
        total += read_res.value;
        header_count = MAX_HEADERS;
        parsed = phr_parse_request(buf, total, &method, &method_len, &path, &path_len,
                                   &minor, headers, &header_count, last);
        if (parsed > 0) break;
        if (parsed == -1) { free(buf); loop_close(stream); return; }
        last = total;
    }
    (void)minor; (void)headers; (void)header_count;
    if (parsed <= 0 || !(method_len == 3 && memcmp(method, "GET", 3) == 0)) {
        send_response(stream, 404, "text/plain; charset=utf-8", "not found\n", 10);
        free(buf); loop_close(stream); return;
    }
    char full_path[2048];
    size_t copy = path_len < sizeof(full_path) - 1 ? path_len : sizeof(full_path) - 1;
    memcpy(full_path, path, copy); full_path[copy] = 0;
    if (full_path[0] == '/' && (full_path[1] == 0 || full_path[1] == '?')) {
        struct picomesh_void_result page_res = render_page(stream, loop, cfg, full_path);
        if (PICOMESH_IS_ERR(page_res)) {
            picomesh_error_print(stderr, "picotrace: render_page", page_res.error);
            picomesh_error_destroy(page_res.error);
        }
    } else if (strcmp(full_path, "/echarts.min.js") == 0) {
        /* Vendored ECharts, embedded in the binary — served locally so the
         * trace UI works with no CDN and in an airgapped environment. */
        send_response(stream, 200, "application/javascript; charset=utf-8",
                      picotrace_echarts_js, (size_t)picotrace_echarts_js_len);
    } else {
        send_response(stream, 404, "text/plain; charset=utf-8", "not found\n", 10);
    }
    free(buf);
    loop_close(stream);
}

static const struct argv_option_def OPTIONS[] = {
    {"--upstream-service", NULL, "upstream_service", "Mesh upstream service name (default trace_collector)", ARGV_VALUE, 0},
    {"--upstream-host", NULL, "upstream_host", "Resolved upstream host (default 127.0.0.1)", ARGV_VALUE, 0},
    {"--upstream-port", NULL, "upstream_port", "Resolved upstream port", ARGV_VALUE, 0},
    {"--host", NULL, "host", "Bind address (default 127.0.0.1)", ARGV_VALUE, 0},
    {"--port", NULL, "port", "Bind port (default 8232)", ARGV_VALUE, 0},
    {"--verbose", "-v", "verbose", "Enable debug logging", ARGV_BOOL, 0},
    {"--help", "-h", "help", "Show usage", ARGV_BOOL, 0},
};

static void usage(const char *prog)
{
    fprintf(stderr, "usage: %s [--upstream-service trace_collector] [--upstream-host 127.0.0.1] [--upstream-port PORT] [--host 127.0.0.1] [--port 8232]\n", prog);
}

int main(int argc, char **argv)
{
    setbuf(stdout, NULL); setbuf(stderr, NULL);

    /* Ignore SIGPIPE process-wide. A write to a peer that has closed its end
     * must surface as EPIPE on the syscall, not kill the process. The upstream
     * collector connection (a raw blocking fd) and the client response writes
     * both go to short-lived peers; under load the collector drops a query
     * mid-call, and without this the next write would terminate picotrace.
     * The mesh-hosted services get this from engine; this standalone app does
     * not go through engine, so it must set it itself. */
    signal(SIGPIPE, SIG_IGN);

    struct argv_chain_ptr_result parse_res = argv_parse(OPTIONS, sizeof(OPTIONS) / sizeof(OPTIONS[0]), argc, argv);
    if (PICOMESH_IS_ERR(parse_res)) {
        fprintf(stderr, "picotrace: argv parse: %s\n", parse_res.error.msg ? parse_res.error.msg : "?");
        picomesh_error_destroy(parse_res.error);
        return 2;
    }
    struct argv_chain *cli = parse_res.value;
    if (argv_get_bool(cli, "help", 0)) { usage(argv[0]); argv_chain_destroy(cli); return 0; }
    if (argv_get_bool(cli, "verbose", 0)) ytrace_set_all_enabled(true);

    const char *host = argv_get_string(cli, "host", "127.0.0.1");
    int port = (int)argv_get_int(cli, "port", 8232);

    struct app_config cfg = {
        .upstream_service = argv_get_string(cli, "upstream_service", "trace_collector"),
        .upstream_host = argv_get_string(cli, "upstream_host", "127.0.0.1"),
        .upstream_port = (int)argv_get_int(cli, "upstream_port", 0),
    };
    if (cfg.upstream_port <= 0) {
        fprintf(stderr, "picotrace: --upstream-port is required when running outside mesh reconciliation\n");
        argv_chain_destroy(cli);
        return 1;
    }
    struct loop_ptr_result loop_res = loop_create();
    if (PICOMESH_IS_ERR(loop_res)) {
        fprintf(stderr, "picotrace: loop_create: %s\n", loop_res.error.msg ? loop_res.error.msg : "?");
        picomesh_error_destroy(loop_res.error);
        argv_chain_destroy(cli);
        return 1;
    }
    struct loop *loop = loop_res.value;
    struct picomesh_void_result listen_res = loop_listen_tcp(loop, host, port, serve_one, &cfg);
    if (PICOMESH_IS_ERR(listen_res)) {
        fprintf(stderr, "picotrace: listen: %s\n", listen_res.error.msg ? listen_res.error.msg : "?");
        picomesh_error_destroy(listen_res.error);
        loop_destroy(loop);
        argv_chain_destroy(cli);
        return 1;
    }
    yinfo("picotrace: listening on %s:%d (upstream=%s %s:%d)", host, port, cfg.upstream_service, cfg.upstream_host, cfg.upstream_port);
    struct picomesh_void_result run_res = loop_run(loop);
    if (PICOMESH_IS_ERR(run_res)) { picomesh_error_print(stderr, "picotrace: collector query", run_res.error); picomesh_error_destroy(run_res.error); }
    loop_destroy(loop);
    argv_chain_destroy(cli);
    return 0;
}
