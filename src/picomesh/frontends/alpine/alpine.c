/* alpine — generic service-console frontend (gh#15).
 *
 * Selected with `frontend: alpine`. Serves the generic `/_alpine` service
 * console (a self-contained HTML/JS page) and proxies the console's JSON
 * calls to a configured upstream yhttp endpoint (a yrpc->yhttp bridge or
 * the gateway):
 *
 *   GET  /            -> the console page
 *   GET  /_alpine     -> the console page
 *   *    /_describe[_tree], /<path>/_describe[_tree], POST /_rpc
 *                     -> proxied verbatim to upstream over yhttp
 *
 * The page knows nothing about any plugin or route: it builds its whole
 * UI from `/_describe` and invokes through JSON `/_rpc`, so it works
 * against any yhttp-compatible endpoint. It is neither the transport
 * bridge nor the picoforge webapp.
 *
 * Access control is explicit: this console can reach every service the
 * upstream exposes, so the node binds 127.0.0.1 by default and, when a
 * `token` is configured, every request must carry it (Authorization:
 * Bearer <token> or ?token=<token>). The console page bootstraps the
 * token from its own `?token=` query and replays it as a bearer header. */

#define _POSIX_C_SOURCE 200809L

#include <picomesh/frontends/alpine/alpine.h>
#include <picomesh/yengine/engine.h>
#include <picomesh/yloop/yloop.h>
#include <picomesh/yconfig/yconfig.h>
#include <picomesh/yargv/yargv.h>
#include <picomesh/ycore/result.h>
#include <picomesh/ycore/ytrace.h>

#include <picohttpparser.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ALPINE_REQ_BUF     (256 * 1024)
#define ALPINE_RESP_BUF    (1024 * 1024)
#define ALPINE_MAX_HEADERS 64

struct alpine_frontend {
    struct picomesh_engine *engine;
    char up_host[128];
    int up_port;
    char *token; /* NULL when no token configured */
};

/* ---- the console page (self-contained: no external scripts) ---------- */

static const char ALPINE_CONSOLE_HTML[] =
"<!doctype html>\n"
"<html lang=\"en\"><head><meta charset=\"utf-8\">\n"
"<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
"<title>picomesh service console</title>\n"
"<style>\n"
" :root{--bg:#0b1014;--lift:#141a1f;--row:#1e262c;--bd:#364a47;--fg:#e0e5e4;\n"
"       --mut:#9fa7a8;--faint:#556162;--acc:#6ba892;--acc2:#74c5a5;--err:#e06c6c}\n"
" *{box-sizing:border-box}\n"
" body{margin:0;font:14px/1.5 system-ui,sans-serif;background:var(--bg);color:var(--fg);height:100vh;display:flex;flex-direction:column}\n"
" header{padding:9px 16px;border-bottom:1px solid var(--bd);display:flex;gap:10px;align-items:center}\n"
" header h1{font-size:14px;margin:0;color:var(--acc)}\n"
" header .up{color:var(--faint);font-size:12px;margin-left:auto;font-family:ui-monospace,monospace}\n"
" button{background:var(--row);color:var(--fg);border:1px solid var(--bd);border-radius:4px;padding:4px 9px;cursor:pointer;font:inherit}\n"
" button:hover{border-color:var(--acc)}\n"
" main{flex:1;display:flex;min-height:0}\n"
" #tree{width:38%;max-width:460px;overflow:auto;padding:10px 8px;border-right:1px solid var(--bd)}\n"
" #panel{flex:1;overflow:auto;padding:16px 20px}\n"
" .svc>.hd,.cls>.hd{display:flex;gap:6px;align-items:center;cursor:pointer;padding:3px 4px;border-radius:4px}\n"
" .svc>.hd:hover,.cls>.hd:hover{background:var(--lift)}\n"
" .svc>.hd{font-weight:600}\n"
" .cls{margin-left:14px}\n"
" .cls>.hd{color:var(--mut);font-size:13px;font-family:ui-monospace,monospace}\n"
" .tw{width:12px;display:inline-block;color:var(--faint)}\n"
" .badge{font-size:10px;color:var(--faint);border:1px solid var(--bd);border-radius:3px;padding:0 5px}\n"
" .meths{margin-left:26px;display:none}\n"
" .meths.open{display:block}\n"
" .cls.closed>.meths,.svc.closed>.body{display:none}\n"
" .m{display:block;width:100%;text-align:left;border:0;background:none;color:var(--fg);\n"
"    font-family:ui-monospace,monospace;font-size:12.5px;padding:2px 6px;border-radius:4px;cursor:pointer}\n"
" .m:hover{background:var(--lift)}\n"
" .m.sel{background:var(--row);color:var(--acc2)}\n"
" h2.path{font-family:ui-monospace,monospace;color:var(--acc);font-size:16px;margin:0 0 4px;word-break:break-all}\n"
" .sub{color:var(--faint);font-size:12px;margin:0 0 18px}\n"
" .field{margin:0 0 12px;max-width:640px}\n"
" .field label{display:block;font-size:12px;margin-bottom:3px}\n"
" .field .ty{color:var(--faint);font-family:ui-monospace,monospace}\n"
" .field input{width:100%;background:var(--bg);color:var(--fg);border:1px solid var(--bd);\n"
"   border-radius:4px;padding:6px 8px;font:13px ui-monospace,monospace}\n"
" .field input:focus{outline:0;border-color:var(--acc)}\n"
" .actions{margin:16px 0;display:flex;gap:8px}\n"
" .actions .go{background:var(--acc);color:#06120d;border-color:var(--acc);font-weight:600}\n"
" pre{background:#000;border:1px solid var(--bd);border-radius:5px;padding:10px;\n"
"   white-space:pre-wrap;word-break:break-all;max-height:48vh;overflow:auto;margin:0}\n"
" .err{color:var(--err)}\n"
" .hint{color:var(--faint)}\n"
" .rbar{display:flex;align-items:center;gap:10px;margin-bottom:8px}\n"
" .rstatus{font-family:ui-monospace,monospace;font-size:12px;color:var(--mut)}\n"
" .rstatus.err{color:var(--err)}\n"
" .rtog{font-size:11px;padding:2px 7px;margin-left:auto}\n"
" .rbody{max-height:52vh;overflow:auto}\n"
" table.rt{border-collapse:collapse;font-size:13px;width:auto}\n"
" table.rt th,table.rt td{border:1px solid var(--bd);padding:3px 9px;text-align:left;vertical-align:top}\n"
" table.rt thead th{background:var(--row);color:var(--mut);font-weight:600}\n"
" table.rt tbody th{background:var(--lift);color:var(--faint);font-weight:500;font-family:ui-monospace,monospace}\n"
" table.rt td{font-family:ui-monospace,monospace}\n"
" .rt-list{margin:0;padding-left:20px}\n"
" .rt-null{color:var(--faint)}\n"
"</style></head>\n"
"<body>\n"
"<header><h1>picomesh service console</h1>\n"
"<span class=\"hint\">reflected from /_describe</span>\n"
"<span class=\"up\" id=\"up\"></span><button id=\"reload\">reload</button></header>\n"
"<main>\n"
"  <div id=\"tree\">loading...</div>\n"
"  <div id=\"panel\"><p class=\"hint\">pick a method on the left</p></div>\n"
"</main>\n"
"<script>\n"
"const TOKEN=new URLSearchParams(location.search).get('token');\n"
"function authH(h){h=h||{};if(TOKEN)h['Authorization']='Bearer '+TOKEN;return h;}\n"
"const $=(t,c,x)=>{const e=document.createElement(t);if(c)e.className=c;if(x!=null)e.textContent=x;return e;};\n"
"async function jget(u){const r=await fetch(u,{headers:authH({})});const t=await r.text();let d;try{d=JSON.parse(t);}catch(e){d=t;}return{ok:r.ok,status:r.status,data:d};}\n"
"async function jrpc(path,args){const r=await fetch('/_rpc',{method:'POST',headers:authH({'Content-Type':'application/json'}),\n"
"  body:JSON.stringify({path,args,kwargs:{}})});const t=await r.text();let d;try{d=JSON.parse(t);}catch(e){d=t;}return{ok:r.ok,status:r.status,data:d};}\n"
"\n"
"// coerce a form string into the JSON type the C method expects\n"
"function coerce(type,v){\n"
"  const t=(type||'').toLowerCase();\n"
"  if(t.includes('bool')) return ['1','true','yes','y','on'].includes(String(v).toLowerCase());\n"
"  if(/\\b(u?int|int\\d+|size_t|long|unsigned)\\b/.test(t)||/int/.test(t)){\n"
"    if(v==='') return 0; const n=parseInt(v,10); return Number.isNaN(n)?v:n;}\n"
"  if(t.includes('float')||t.includes('double')){\n"
"    if(v==='') return 0; const n=parseFloat(v); return Number.isNaN(n)?v:n;}\n"
"  return String(v);                              // char * and everything else\n"
"}\n"
"\n"
"// ---- result renderer: JSON value -> HTML (array-of-objects -> table) ----\n"
"function esc(s){return String(s).replace(/[&<>\"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','\"':'&quot;',\"'\":'&#39;'}[c]));}\n"
"function isObj(v){return v!==null&&typeof v==='object'&&!Array.isArray(v);}\n"
"function renderValue(v){\n"
"  if(v===null||v===undefined) return '<span class=\"rt-null\">null</span>';\n"
"  if(typeof v==='boolean') return v?'true':'false';\n"
"  if(typeof v==='number') return esc(String(v));\n"
"  if(typeof v==='string'){                              // a string that is itself JSON -> render recursively\n"
"    const s=v.trim();\n"
"    if(s.length>1&&(s[0]==='{'||s[0]==='[')){try{const p=JSON.parse(s);if(p&&typeof p==='object')return renderValue(p);}catch(e){}}\n"
"    return esc(v);\n"
"  }\n"
"  if(Array.isArray(v)){\n"
"    if(!v.length) return '<span class=\"rt-null\">[ empty ]</span>';\n"
"    if(v.every(isObj)){                                  // list of records -> table\n"
"      const keys=[...new Set(v.flatMap(o=>Object.keys(o)))];\n"
"      let h='<table class=\"rt\"><thead><tr><th>#</th>'+keys.map(k=>'<th>'+esc(k)+'</th>').join('')+'</tr></thead><tbody>';\n"
"      v.forEach((row,i)=>{h+='<tr><th>'+i+'</th>'+keys.map(k=>'<td>'+(k in row?renderValue(row[k]):'<span class=\"rt-null\">&mdash;</span>')+'</td>').join('')+'</tr>';});\n"
"      return h+'</tbody></table>';\n"
"    }\n"
"    return '<ol class=\"rt-list\">'+v.map(x=>'<li>'+renderValue(x)+'</li>').join('')+'</ol>';\n"
"  }\n"
"  if(isObj(v)){\n"
"    const e=Object.entries(v);\n"
"    if(!e.length) return '<span class=\"rt-null\">{ empty }</span>';\n"
"    return '<table class=\"rt\"><tbody>'+e.map(([k,val])=>'<tr><th>'+esc(k)+'</th><td>'+renderValue(val)+'</td></tr>').join('')+'</tbody></table>';\n"
"  }\n"
"  return esc(String(v));\n"
"}\n"
"function showResult(out,res){\n"
"  out.innerHTML='';\n"
"  const bar=$('div','rbar');\n"
"  bar.appendChild($('span',res.ok?'rstatus':'rstatus err','HTTP '+res.status));\n"
"  const tog=$('button','rtog','raw json');bar.appendChild(tog);\n"
"  // unwrap {result: ...} / {error: ...}\n"
"  let val=res.data;\n"
"  if(val&&typeof val==='object'){ if('result'in val)val=val.result; else if('error'in val)val=val.error; }\n"
"  const tbl=$('div','rbody');tbl.innerHTML=renderValue(val);\n"
"  const raw=$('pre','rbody');raw.style.display='none';\n"
"  raw.textContent=typeof res.data==='string'?res.data:JSON.stringify(res.data,null,2);\n"
"  let showingRaw=false;\n"
"  tog.onclick=()=>{showingRaw=!showingRaw;tbl.style.display=showingRaw?'none':'';raw.style.display=showingRaw?'':'none';tog.textContent=showingRaw?'table':'raw json';};\n"
"  out.appendChild(bar);out.appendChild(tbl);out.appendChild(raw);\n"
"}\n"
"\n"
"let SELECTED=null;\n"
"async function loadTree(){\n"
"  const tree=document.getElementById('tree');\n"
"  const r=await jget('/_describe');\n"
"  if(!r.ok){tree.innerHTML='';tree.appendChild($('div','err','/_describe HTTP '+r.status));return;}\n"
"  const svcs=(r.data&&r.data.services)||[];\n"
"  tree.innerHTML='';\n"
"  if(!svcs.length){tree.appendChild($('div','hint','no active services'));return;}\n"
"  for(const s of svcs){\n"
"    const box=$('div','svc closed');\n"
"    const hd=$('div','hd');hd.appendChild($('span','tw','\\u25B6'));\n"
"    hd.appendChild($('span',null,s.service));hd.appendChild($('span','badge',s.source||'?'));\n"
"    hd.onclick=()=>{box.classList.toggle('closed');hd.querySelector('.tw').textContent=box.classList.contains('closed')?'\\u25B6':'\\u25BC';};\n"
"    box.appendChild(hd);\n"
"    const body=$('div','body');\n"
"    for(const c of (s.classes||[])){\n"
"      const cb=$('div','cls closed');\n"
"      const clsName=c.class.indexOf(s.service+'.')===0?c.class.slice(s.service.length+1):c.class;\n"
"      const ch=$('div','hd');ch.appendChild($('span','tw','\\u25B6'));ch.appendChild($('span',null,clsName));\n"
"      ch.onclick=()=>{cb.classList.toggle('closed');ch.querySelector('.tw').textContent=cb.classList.contains('closed')?'\\u25B6':'\\u25BC';};\n"
"      cb.appendChild(ch);\n"
"      const ms=$('div','meths open');\n"
"      const pre=(c.qname||'')+'_';\n"
"      for(const mq of (c.methods||[])){\n"
"        const verb=mq.indexOf(pre)===0?mq.slice(pre.length):mq;\n"
"        const path=c.class+'.'+verb;\n"
"        const b=$('button','m',verb);b.dataset.path=path;\n"
"        b.onclick=()=>{document.querySelectorAll('.m.sel').forEach(x=>x.classList.remove('sel'));b.classList.add('sel');loadMethod(path);};\n"
"        ms.appendChild(b);\n"
"      }\n"
"      cb.appendChild(ms);body.appendChild(cb);\n"
"    }\n"
"    box.appendChild(body);tree.appendChild(box);\n"
"  }\n"
"  // deep-link / demo: ?expand=1 opens the whole tree, ?path=svc.cls.verb\n"
"  // pre-selects a method (so a single screenshot shows the fields panel).\n"
"  const q=new URLSearchParams(location.search);\n"
"  const wp=q.get('path');\n"
"  if(q.get('expand')==='1'||wp){\n"
"    document.querySelectorAll('.svc,.cls').forEach(x=>x.classList.remove('closed'));\n"
"    document.querySelectorAll('.tw').forEach(t=>t.textContent='\\u25BC');\n"
"  }\n"
"  if(wp){\n"
"    const b=[...document.querySelectorAll('.m')].find(x=>x.dataset.path===wp);\n"
"    if(b)b.classList.add('sel');\n"
"    loadMethod(wp);\n"
"  }\n"
"}\n"
"\n"
"async function loadMethod(path){\n"
"  SELECTED=path;\n"
"  const q=new URLSearchParams(location.search);\n"
"  const panel=document.getElementById('panel');panel.innerHTML='';\n"
"  panel.appendChild(Object.assign($('h2','path',path),{}));\n"
"  const r=await jget('/'+path+'/_describe');\n"
"  if(!r.ok){panel.appendChild($('div','err','describe HTTP '+r.status));return;}\n"
"  const params=(r.data&&r.data.params)||[];\n"
"  panel.appendChild($('p','sub',params.length?(params.length+' parameter'+(params.length>1?'s':'')):'no parameters'));\n"
"  const form=$('form');const inputs=[];\n"
"  for(const p of params){\n"
"    const f=$('div','field');\n"
"    const lab=$('label');lab.appendChild($('span',null,p.name+' '));lab.appendChild($('span','ty',': '+p.type));\n"
"    f.appendChild(lab);\n"
"    const inp=$('input');inp.placeholder=p.type;inp.dataset.type=p.type;inp.dataset.name=p.name;\n"
"    f.appendChild(inp);form.appendChild(f);inputs.push(inp);\n"
"  }\n"
"  const fillRaw=q.get('args');                    // demo deep-link: prefill fields\n"
"  if(fillRaw){try{const vals=JSON.parse(fillRaw);inputs.forEach((i,ix)=>{if(ix<vals.length)i.value=String(vals[ix]);});}catch(e){}}\n"
"  const act=$('div','actions');\n"
"  const go=$('button','go','Invoke');go.type='button';\n"
"  const out=$('div','result');\n"
"  go.onclick=async()=>{\n"
"    const args=inputs.map(i=>coerce(i.dataset.type,i.value));\n"
"    out.innerHTML='<span class=\"hint\">...</span>';\n"
"    const res=await jrpc(path,args);\n"
"    showResult(out,res);\n"
"  };\n"
"  act.appendChild(go);\n"
"  form.appendChild(act);panel.appendChild(form);\n"
"  panel.appendChild($('div','hint','args sent positionally as ['+params.map(p=>p.name).join(', ')+']'));\n"
"  panel.appendChild(out);\n"
"  if(q.get('invoke')==='1')go.click();            // demo deep-link: auto-run\n"
"}\n"
"\n"
"document.getElementById('reload').onclick=loadTree;\n"
"document.getElementById('up').textContent=location.host;\n"
"loadTree();\n"
"</script>\n"
"</body></html>";

/* ---- tiny HTTP helpers ----------------------------------------------- */

extern size_t yloop_write(struct yloop_stream *s, const void *buf, size_t n);

static void send_resp(struct yloop_stream *stream, int status, const char *reason,
                      const char *ctype, const char *body, size_t blen)
{
    char header[512];
    int header_len = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "Cache-Control: no-store, no-cache, must-revalidate\r\n"
        "Pragma: no-cache\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type, Authorization\r\n"
        "\r\n",
        status, reason, ctype, blen);
    if (header_len <= 0) return;
    yloop_write(stream, header, (size_t)header_len);
    if (blen) yloop_write(stream, body, blen);
}

static int hdr_match(const struct phr_header *hdrs, size_t count, const char *want,
                     char *out, size_t out_cap)
{
    size_t want_len = strlen(want);
    for (size_t i = 0; i < count; ++i) {
        if (hdrs[i].name_len != want_len) continue;
        if (strncasecmp(hdrs[i].name, want, want_len) != 0) continue;
        size_t copy_len = hdrs[i].value_len < out_cap - 1 ? hdrs[i].value_len : out_cap - 1;
        memcpy(out, hdrs[i].value, copy_len);
        out[copy_len] = 0;
        return 1;
    }
    return 0;
}

/* Compare request path (ignoring any ?query) to a literal. */
static int path_eq(const char *path, const char *target)
{
    const char *query = strchr(path, '?');
    size_t path_len = query ? (size_t)(query - path) : strlen(path);
    return path_len == strlen(target) && memcmp(path, target, path_len) == 0;
}

static int path_ends(const char *path, const char *suffix)
{
    const char *query = strchr(path, '?');
    size_t path_len = query ? (size_t)(query - path) : strlen(path);
    size_t suffix_len = strlen(suffix);
    return path_len > suffix_len && memcmp(path + path_len - suffix_len, suffix, suffix_len) == 0;
}

/* Extract ?key=value from the path. Returns 1 on hit. */
static int query_get(const char *path, const char *key, char *out, size_t out_cap)
{
    const char *query = strchr(path, '?');
    if (!query) return 0;
    size_t key_len = strlen(key);
    const char *scan = query + 1;
    while (*scan) {
        const char *equals = strchr(scan, '=');
        const char *ampersand = strchr(scan, '&');
        if (equals && (!ampersand || equals < ampersand) && (size_t)(equals - scan) == key_len && memcmp(scan, key, key_len) == 0) {
            const char *value_end = ampersand ? ampersand : scan + strlen(scan);
            size_t value_len = (size_t)(value_end - equals - 1);
            if (value_len >= out_cap) value_len = out_cap - 1;
            memcpy(out, equals + 1, value_len);
            out[value_len] = 0;
            return 1;
        }
        if (!ampersand) break;
        scan = ampersand + 1;
    }
    return 0;
}

/* True for the JSON API surface the console drives — proxied upstream. */
static int wants_proxy(const char *path, int is_get, int is_post)
{
    if (is_post && path_eq(path, "/_rpc")) return 1;
    if (is_get || is_post) {
        if (path_eq(path, "/_describe") || path_eq(path, "/_describe_tree")) return 1;
        if (path_ends(path, "/_describe") || path_ends(path, "/_describe_tree")) return 1;
    }
    return 0;
}

static int authorized(const struct alpine_frontend *frontend, const struct phr_header *hdrs,
                      size_t header_count, const char *path)
{
    if (!frontend->token) return 1; /* no token configured: open (loopback admin tool) */
    char got[300] = {0};
    char auth[300] = {0};
    if (hdr_match(hdrs, header_count, "authorization", auth, sizeof(auth))) {
        const char *value = auth;
        if (strncasecmp(value, "Bearer ", 7) == 0) value += 7;
        while (*value == ' ') ++value;
        snprintf(got, sizeof(got), "%s", value);
    }
    if (!got[0]) query_get(path, "token", got, sizeof(got));
    return got[0] && strcmp(got, frontend->token) == 0;
}

/* Open a fresh upstream connection, forward the request, relay the full
 * close-delimited response back to the browser. The console token (if any)
 * is NOT forwarded — it gates the console, not the upstream. */
static void proxy_upstream(struct yloop *loop, struct yloop_stream *client,
                           const struct alpine_frontend *frontend,
                           const char *method, const char *path,
                           const char *ctype, const char *body, size_t blen)
{
    struct yloop_stream_ptr_result upstream_res = yloop_connect_tcp(loop, frontend->up_host, frontend->up_port);
    if (PICOMESH_IS_ERR(upstream_res)) {
        picomesh_error_destroy(upstream_res.error);
        static const char msg[] = "{\"error\":\"alpine: upstream connect failed\"}";
        send_resp(client, 502, "Bad Gateway", "application/json", msg, sizeof(msg) - 1);
        return;
    }
    struct yloop_stream *upstream = upstream_res.value;

    char head[4096];
    int head_len = snprintf(head, sizeof(head),
        "%s %s HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Connection: close\r\n",
        method, path, frontend->up_host, frontend->up_port);
    if (head_len > 0 && ctype && *ctype)
        head_len += snprintf(head + head_len, sizeof(head) - (size_t)head_len, "Content-Type: %s\r\n", ctype);
    if (head_len > 0)
        head_len += snprintf(head + head_len, sizeof(head) - (size_t)head_len, "Content-Length: %zu\r\n\r\n", blen);
    if (head_len <= 0 || (size_t)head_len >= sizeof(head)) {
        yloop_close(upstream);
        static const char msg[] = "{\"error\":\"alpine: request too large to proxy\"}";
        send_resp(client, 502, "Bad Gateway", "application/json", msg, sizeof(msg) - 1);
        return;
    }
    yloop_write(upstream, head, (size_t)head_len);
    if (blen) yloop_write(upstream, body, blen);

    /* Upstream was asked to close, so read to EOF == full response. */
    char *response = malloc(ALPINE_RESP_BUF);
    if (!response) { yloop_close(upstream); yloop_close(client); return; }
    size_t response_len = 0;
    for (;;) {
        if (response_len >= ALPINE_RESP_BUF) break;
        size_t got = yloop_read_some(upstream, response + response_len, ALPINE_RESP_BUF - response_len);
        if (got == 0) break;
        response_len += got;
    }
    yloop_close(upstream);
    if (response_len) yloop_write(client, response, response_len);
    free(response);
}

/* ---- per-connection serve coroutine (one request, then close) -------- */

static void serve_one(struct yloop *loop, struct yloop_stream *stream, void *ud)
{
    struct alpine_frontend *frontend = ud;
    char *buf = malloc(ALPINE_REQ_BUF);
    if (!buf) { yloop_close(stream); return; }

    size_t buf_len = 0;
    int minor = 0;
    const char *method_ptr = NULL, *path = NULL;
    size_t method_len = 0, path_len = 0;
    struct phr_header hdrs[ALPINE_MAX_HEADERS];
    size_t header_count = 0;
    int parsed = -2;

    while (parsed == -2) {
        if (buf_len >= ALPINE_REQ_BUF) goto done;
        size_t chunk = ALPINE_REQ_BUF - buf_len;
        if (chunk > 4096) chunk = 4096;
        size_t got = yloop_read_some(stream, buf + buf_len, chunk);
        if (got == 0) goto done;
        buf_len += got;
        header_count = ALPINE_MAX_HEADERS;
        parsed = phr_parse_request(buf, buf_len, &method_ptr, &method_len, &path, &path_len, &minor, hdrs, &header_count, 0);
    }
    if (parsed < 0) goto done;
    size_t header_end = (size_t)parsed;

    long clen = 0;
    char content_len_str[32] = {0};
    if (hdr_match(hdrs, header_count, "Content-Length", content_len_str, sizeof(content_len_str))) clen = strtol(content_len_str, NULL, 10);
    if (clen > 0) {
        size_t have = buf_len - header_end;
        while ((long)have < clen) {
            if (header_end + (size_t)clen > ALPINE_REQ_BUF) goto done;
            size_t got = yloop_read_some(stream, buf + buf_len, (size_t)clen - have);
            if (got == 0) goto done;
            buf_len += got;
            have += got;
        }
    }
    const char *body = buf + header_end;
    ((char *)path)[path_len] = 0; /* NUL-terminate path (byte after is part of HTTP/1.1) */

    char method[16] = {0};
    memcpy(method, method_ptr, method_len < sizeof(method) - 1 ? method_len : sizeof(method) - 1);
    int is_get = strcmp(method, "GET") == 0;
    int is_post = strcmp(method, "POST") == 0;
    int is_opt = strcmp(method, "OPTIONS") == 0;

    if (!authorized(frontend, hdrs, header_count, path)) {
        static const char msg[] =
            "{\"error\":\"unauthorized: pass ?token= or Authorization: Bearer <token>\"}";
        send_resp(stream, 401, "Unauthorized", "application/json", msg, sizeof(msg) - 1);
        goto done;
    }

    if (is_opt) {
        send_resp(stream, 200, "OK", "text/plain", "", 0);
        goto done;
    }
    if (is_get && (path_eq(path, "/") || path_eq(path, "/_alpine"))) {
        send_resp(stream, 200, "OK", "text/html; charset=utf-8",
                  ALPINE_CONSOLE_HTML, sizeof(ALPINE_CONSOLE_HTML) - 1);
        goto done;
    }
    if (wants_proxy(path, is_get, is_post)) {
        char ctype[128] = {0};
        hdr_match(hdrs, header_count, "Content-Type", ctype, sizeof(ctype));
        proxy_upstream(loop, stream, frontend, method, path, ctype[0] ? ctype : "application/json",
                       body, (size_t)clen);
        goto done;
    }

    {
        static const char not_found[] =
            "{\"error\":\"alpine: no such route (serves /_alpine; proxies /_describe and /_rpc)\"}";
        send_resp(stream, 404, "Not Found", "application/json", not_found, sizeof(not_found) - 1);
    }

done:
    free(buf);
    yloop_close(stream);
}

/* ---- config + start -------------------------------------------------- */

/* Fetch `alpine.<suffix>` for this node, honoring the engine's service
 * projection (top level) with a fallback to the un-projected parent path. */
static const struct yconfig_node *alpine_cfg(struct picomesh_engine *engine, const char *suffix)
{
    const struct yconfig *cfg = picomesh_engine_config(engine);
    if (!cfg) return NULL;
    struct yargv_chain *cli = picomesh_engine_cli(engine);
    const char *name = cli ? yargv_get_string(cli, "name", NULL) : NULL;
    char path[256];
    if (name && *name) {
        snprintf(path, sizeof(path), "mesh.services.%s.config.alpine.%s", name, suffix);
        struct yconfig_node_ptr_result node_res = yconfig_get(cfg, path);
        if (PICOMESH_IS_OK(node_res) && node_res.value) return node_res.value;
        if (PICOMESH_IS_ERR(node_res)) picomesh_error_destroy(node_res.error);
    }
    snprintf(path, sizeof(path), "alpine.%s", suffix);
    struct yconfig_node_ptr_result node_res = yconfig_get(cfg, path);
    if (PICOMESH_IS_OK(node_res) && node_res.value) return node_res.value;
    if (PICOMESH_IS_ERR(node_res)) picomesh_error_destroy(node_res.error);
    return NULL;
}

struct alpine_upstream_fields {
    char host[128];
    int port;
};

static int alpine_upstream_cb(const char *key, const struct yconfig_node *val, void *ud)
{
    struct alpine_upstream_fields *upstream = ud;
    if (strcmp(key, "host") == 0) {
        const char *host = yconfig_node_as_string(val, NULL);
        if (host) snprintf(upstream->host, sizeof(upstream->host), "%s", host);
    } else if (strcmp(key, "port") == 0) {
        upstream->port = (int)yconfig_node_as_int(val, 0);
    }
    return 0;
}

struct alpine_frontend_ptr_result alpine_start(struct picomesh_engine *engine,
                                               const struct alpine_config *cfg)
{
    if (!engine) return PICOMESH_ERR(alpine_frontend_ptr, "alpine_start: NULL engine");
    const char *host = (cfg && cfg->host) ? cfg->host : "127.0.0.1";
    int port = (cfg && cfg->port > 0) ? cfg->port : 8231;

    struct yloop *loop = picomesh_engine_loop(engine);
    if (!loop) return PICOMESH_ERR(alpine_frontend_ptr, "alpine_start: engine has no loop");

    /* Resolve the upstream yhttp endpoint this console proxies to. */
    struct alpine_upstream_fields upstream = {.host = "127.0.0.1", .port = 0};
    const struct yconfig_node *upstream_node = alpine_cfg(engine, "upstream");
    if (upstream_node && yconfig_node_kind(upstream_node) == YCONFIG_MAP)
        yconfig_node_for_each(upstream_node, alpine_upstream_cb, &upstream);
    if (upstream.port <= 0)
        return PICOMESH_ERR(alpine_frontend_ptr,
                            "alpine_start: config.alpine.upstream.port is required "
                            "(the yhttp endpoint the console proxies to)");

    struct alpine_frontend *frontend = calloc(1, sizeof(*frontend));
    if (!frontend) return PICOMESH_ERR(alpine_frontend_ptr, "alpine_start: calloc failed");
    frontend->engine = engine;
    snprintf(frontend->up_host, sizeof(frontend->up_host), "%s", upstream.host[0] ? upstream.host : "127.0.0.1");
    frontend->up_port = upstream.port;

    const struct yconfig_node *token_node = alpine_cfg(engine, "token");
    const char *token = token_node ? yconfig_node_as_string(token_node, NULL) : NULL;
    if (token && *token) {
        frontend->token = strdup(token);
        if (!frontend->token) { free(frontend); return PICOMESH_ERR(alpine_frontend_ptr, "alpine_start: strdup failed"); }
    }

    struct picomesh_void_result listen_res = yloop_listen_tcp(loop, host, port, serve_one, frontend);
    if (PICOMESH_IS_ERR(listen_res)) {
        free(frontend->token);
        free(frontend);
        return PICOMESH_ERR(alpine_frontend_ptr, "alpine_start: yloop_listen_tcp failed", listen_res);
    }
    yinfo("alpine: console on %s:%d -> upstream %s:%d (%s)",
          host, port, frontend->up_host, frontend->up_port, frontend->token ? "token-gated" : "open");
    return PICOMESH_OK(alpine_frontend_ptr, frontend);
}

void alpine_stop(struct alpine_frontend *frontend)
{
    if (!frontend) return;
    free(frontend->token);
    free(frontend);
}
