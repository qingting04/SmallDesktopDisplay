/* *****************************************************************
 *
 * webui.h  —  SDD 内网设置页面（深色卡片式中文 UI）
 *
 * 两套页面：SDD_PAGE_AP   配网模式（仅WiFi配网）
 *           SDD_PAGE      已联网模式（仅显示设置，无WiFi内容）
 * 整页以 PROGMEM 存储，由 handleRoot() 经 server.send_P() 一次性发送，
 * 避免运行时大 String 拼接造成堆碎片。
 * 注意：本文件必须保持 UTF-8 编码，页面含中文。
 *
 * *****************************************************************/
#ifndef SDD_WEBUI_H
#define SDD_WEBUI_H

#include <pgmspace.h>

/*---------------- AP配网模式页面：仅状态 + WiFi配网 ----------------*/
static const char SDD_PAGE_AP[] PROGMEM = R"rawliteral(<!DOCTYPE html><html lang="zh-CN"><head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>SDD · WiFi配网</title>
<style>
:root{--bg:#14171c;--card:#1f242c;--fg:#e8eaed;--dim:#8b98a9;--acc:#4da3ff;--line:#2e3742}
*{box-sizing:border-box}
body{margin:0;padding:14px;background:var(--bg);color:var(--fg);font-family:-apple-system,"PingFang SC","Microsoft YaHei",sans-serif;font-size:14px;max-width:520px;margin:0 auto}
h1{font-size:17px;margin:6px 2px 14px}
h1 small{color:var(--dim);font-size:12px;font-weight:normal;margin-left:8px}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:14px;margin:12px 0}
.t{color:var(--acc);font-weight:bold;margin-bottom:10px}
.row{display:flex;align-items:center;gap:8px;margin:8px 0;flex-wrap:wrap}
.lab{min-width:88px}
.dim{color:var(--dim)}
input[type=text],input[type=password],select{background:#101318;border:1px solid var(--line);color:var(--fg);border-radius:8px;padding:7px 9px;font-size:14px;width:130px}
.filein{background:#101318;border:1px dashed var(--line);color:var(--dim);border-radius:8px;padding:7px 9px;font-size:12px;flex:1;min-width:150px;max-width:100%}
.btn{background:var(--acc);color:#0b1220;border:0;border-radius:8px;padding:9px 16px;font-size:14px;font-weight:bold;cursor:pointer}
.btn:disabled{opacity:.45;cursor:wait}
.kv{display:flex;justify-content:space-between;border-bottom:1px dashed var(--line);padding:5px 0}
.kv:last-child{border-bottom:0}
.toast{position:fixed;left:50%;bottom:26px;transform:translateX(-50%);background:#2b3442;border:1px solid var(--acc);padding:9px 16px;border-radius:10px;opacity:0;transition:.3s;max-width:92%}
.toast.show{opacity:1}
footer{color:var(--dim);text-align:center;margin:16px 0 6px;font-size:12px}
</style></head><body>
<h1>SDD 桌面显示器<small id="ver">V2.3</small></h1>
<div class="card"><div class="t">设备状态</div><div id="st" class="dim">状态加载中…</div></div>
<div class="card"><div class="t">WiFi 配网</div>
<div class="dim" style="margin-bottom:6px">当前为配网模式：选择或输入家里WiFi，保存后设备重启并自动连接。</div>
<div class="row"><select id="scanlist" style="width:200px"><option value="">— 点击扫描选择，或下方手动输入 —</option></select><button class="btn" id="btnscan" onclick="doScan()">扫描</button></div>
<div class="row"><span class="lab">SSID</span><input id="ssid" type="text" maxlength="31" placeholder="无线网络名称"></div>
<div class="row"><span class="lab">密码</span><input id="psw" type="password" maxlength="63" placeholder="开放网络留空"></div>
<div class="row"><button class="btn" id="btnwifi" onclick="saveWifi()">保存并重启</button><span class="dim">保存后设备重启，约30秒后刷新本页面</span></div>
<div id="scaninfo" class="dim" style="margin-top:6px"></div></div>
<div class="card"><div class="t">固件升级</div>
<div class="row"><input id="fw" class="filein" type="file" accept=".bin"><button class="btn" id="btnota" onclick="doOta()">上传文件</button></div>
<div id="otabox" style="display:none;margin-top:4px"><div style="height:8px;background:#101318;border-radius:4px;overflow:hidden"><div id="otabar" style="height:100%;width:0;background:var(--acc)"></div></div><div id="otamsg" class="dim" style="margin-top:6px"></div></div></div>
<footer>配网完成后，页面将切换为显示设置</footer>
<div class="toast" id="tst"></div>
<script>
var tstT=null,otaBusy=false,OTA_MAX=1040000;
function $(i){return document.getElementById(i)}
function tst(m){var t=$('tst');t.textContent=m;t.classList.add('show');clearTimeout(tstT);tstT=setTimeout(function(){t.classList.remove('show')},2500)}

/* 设备返回的字段级错误码 → 能让人照着改的中文 */
function errText(k){var m={bl:'亮度只能 1-100',upt:'天气更新间隔只能 1-60 分钟',tbl:'分时段亮度只接受 0/1',nbl:'夜间亮度只能 1-100',nst:'夜间开始只接受 0-23 点',net:'夜间结束只接受 0-23 点',cc:'城市代码要 7 位，例如 101250101',cn:'城市名称最多 24 个字',rot:'屏幕方向只能 0-3'};return m[k]||('参数不合法'+(k?'（'+k+'）':''))}
async function req(u,opt){var c=new AbortController();var to=setTimeout(function(){c.abort()},8000);opt=opt||{};opt.signal=c.signal;try{var r=await fetch(u,opt);clearTimeout(to);return await r.json()}catch(e){clearTimeout(to);return null}}
async function refresh(){
if(otaBusy)return;
var s=await req('/api/status');
if(!s){tst('设备无响应，请稍后重试');return}
var wxAge=(s.wxAge===4294967295)?'从未成功':(s.wxAge<120?s.wxAge+' 秒前':(s.wxAge<7200?Math.floor(s.wxAge/60)+' 分钟前':Math.floor(s.wxAge/3600)+' 小时前'));
var k=[['模式','AP配网热点'],['热点名称',s.ssid||'-'],['访问地址',s.ip||'-'],['运行时长',Math.floor(s.up/3600)+'时'+Math.floor(s.up%3600/60)+'分'],['固件版本',s.ver],['天气更新',wxAge+(s.wxErr?'（上次HTTP '+s.wxErr+'）':'')],['天气读数',(s.temp||'-')+' / '+(s.hum||'-')],['可用内存',Math.round(s.heap/1024)+' KB（碎片 '+s.frag+'%）'],['Flash',s.flash+' KB，固件 '+s.sketch+' KB']];
var h='';for(var i=0;i<k.length;i++)h+='<div class="kv"><span>'+k[i][0]+'</span><span>'+k[i][1]+'</span></div>';
$('st').innerHTML=h;$('ver').textContent=s.ver;
}
$('ssid').oninput=function(){$('scanlist').selectedIndex=0};
async function doScan(){
var b=$('btnscan');b.disabled=true;b.textContent='扫描中…';$('scaninfo').textContent='正在扫描周边WiFi（约2-5秒），请稍候…';
var d=await req('/api/scan');
b.disabled=false;b.textContent='扫描';$('scaninfo').textContent='';
if(!d||d.ok!=1){tst('扫描失败，请重试');return}
var sel=$('scanlist');sel.innerHTML='<option value="">— 点击选择网络 —</option>';
for(var i=0;i<d.list.length;i++){var w=d.list[i];var o=document.createElement('option');o.value=w.ssid;o.textContent=w.ssid+'（'+w.rssi+' dBm，信道'+w.ch+'）';sel.appendChild(o)}
$('scaninfo').textContent='扫描到 '+d.list.length+' 个网络，点击列表项可填入SSID';
if(!d.list.length)tst('未扫描到可用网络');
}
$('scanlist').onchange=function(){if(this.value)$('ssid').value=this.value};
async function saveWifi(){
var s=$('ssid').value.trim();
if(!s){tst('请输入或选择SSID');return}
if(!confirm('确认连接 “'+s+'” ？\n设备将保存凭据并重启。'))return;
var b=$('btnwifi');b.disabled=true;b.textContent='保存中…';
var r=await fetch('/save_wifi',{method:'POST',body:new URLSearchParams({ssid:s,psw:$('psw').value})});
var j=await r.json().catch(function(){return null});
if(j&&j.ok==1){document.body.innerHTML='<div style="text-align:center;padding:60px 20px;font-size:16px">已保存，设备重启中…<br><br>约30秒后请刷新本页面<br>连上家里WiFi后页面即为显示设置</div>'}
else{tst('保存失败');b.disabled=false;b.textContent='保存并重启'}
}
/* 升级后设备要重启：轮询 /api/status 直到重新应答，再刷新页面（避免固定等待时间猜不准） */
function waitBack(n){
if(n>28){$('otamsg').textContent='等待超时，请手动刷新页面';return}
$('otamsg').textContent='等待重启'+(n?('（'+(n*3)+'s）'):'');
setTimeout(async function(){
var s=await req('/api/status');
if(s){$('otamsg').textContent='已升级到 '+s.ver;setTimeout(function(){location.reload()},1200)}
else waitBack(n+1)
},3000)}
/* 固件升级：multipart 上传到 /update，设备写完即重启，靠版本号确认 */
function doOta(){
var el=$('fw');
if(!el.files||!el.files.length){tst('先选一个 .bin 文件');return}
var f=el.files[0];
if(!/\.bin$/i.test(f.name)){tst('只支持 .bin 文件');return}
if(f.size>OTA_MAX){tst('文件太大，上限 '+(OTA_MAX/1024|0)+'KB');return}
if(!confirm('上传 '+f.name+'（'+(f.size/1024|0)+'KB）？\n上传期间勿断电。'))return;
var b=$('btnota'),box=$('otabox'),bar=$('otabar'),msg=$('otamsg');
otaBusy=true;b.disabled=true;b.textContent='上传中…';box.style.display='block';bar.style.width='0%';msg.textContent='上传中 0%';
var fd=new FormData();fd.append('firmware',f,f.name);
var x=new XMLHttpRequest();
x.open('POST','/update',true);
x.upload.onprogress=function(e){if(!e.lengthComputable)return;var p=Math.floor(e.loaded/e.total*100);bar.style.width=p+'%';msg.textContent='上传中 '+p+'%';};
x.onload=function(){
var j=null;try{j=JSON.parse(x.responseText)}catch(e2){}
if(j&&j.ok==1){bar.style.width='100%';waitBack(0)}
else{otaBusy=false;b.disabled=false;b.textContent='上传文件';msg.textContent='上传失败'+(j&&j.code?'（错误码 '+j.code+'）':'');}
};
x.onerror=function(){waitBack(0)};
x.send(fd);
}
window.onload=refresh;
setInterval(refresh,8000);
</script>
</body></html>)rawliteral";

/*---------------- 已联网模式页面：仅显示设置（无WiFi内容） ----------------*/
static const char SDD_PAGE[] PROGMEM = R"rawliteral(<!DOCTYPE html><html lang="zh-CN"><head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>SDD · 显示设置</title>
<style>
:root{--bg:#14171c;--card:#1f242c;--fg:#e8eaed;--dim:#8b98a9;--acc:#4da3ff;--line:#2e3742}
*{box-sizing:border-box}
body{margin:0;padding:14px;background:var(--bg);color:var(--fg);font-family:-apple-system,"PingFang SC","Microsoft YaHei",sans-serif;font-size:14px;max-width:520px;margin:0 auto}
h1{font-size:17px;margin:6px 2px 14px}
h1 small{color:var(--dim);font-size:12px;font-weight:normal;margin-left:8px}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:14px;margin:12px 0}
.t{color:var(--acc);font-weight:bold;margin-bottom:10px}
.row{display:flex;align-items:center;gap:8px;margin:8px 0;flex-wrap:wrap}
.lab{min-width:88px}
.dim{color:var(--dim)}
input[type=text],input[type=number]{background:#101318;border:1px solid var(--line);color:var(--fg);border-radius:8px;padding:7px 9px;font-size:14px;width:130px}
select{background:#101318;border:1px solid var(--line);color:var(--fg);border-radius:8px;padding:6px 8px;font-size:14px}
.filein{background:#101318;border:1px dashed var(--line);color:var(--dim);border-radius:8px;padding:7px 9px;font-size:12px;flex:1;min-width:150px;max-width:100%}
input[type=checkbox]{accent-color:var(--acc);width:16px;height:16px}
input[type=range]{flex:1;accent-color:var(--acc)}
input[type=radio]{accent-color:var(--acc)}
.btn{background:var(--acc);color:#0b1220;border:0;border-radius:8px;padding:9px 16px;font-size:14px;font-weight:bold;cursor:pointer}
.btn:disabled{opacity:.45;cursor:wait}
.kv{display:flex;justify-content:space-between;border-bottom:1px dashed var(--line);padding:5px 0}
.kv:last-child{border-bottom:0}
.toast{position:fixed;left:50%;bottom:26px;transform:translateX(-50%);background:#2b3442;border:1px solid var(--acc);padding:9px 16px;border-radius:10px;opacity:0;transition:.3s;max-width:92%}
.toast.show{opacity:1}
footer{color:var(--dim);text-align:center;margin:16px 0 6px;font-size:12px}
</style></head><body>
<h1>SDD 桌面显示器<small id="ver">V2.3</small></h1>
<div class="card"><div class="t">设备状态</div><div id="st" class="dim">状态加载中…</div></div>
<div class="card"><div class="t">显示设置</div>
<div class="row"><span class="lab">亮度·白天</span><input id="bl" type="range" min="1" max="100" onchange="saveCfg()"><span id="blv" style="width:40px"></span></div>
<div class="row"><span class="lab">屏幕方向</span><label><input type="radio" name="rot" value="0"> 0·USB朝下</label><label><input type="radio" name="rot" value="1"> 1·朝右</label><label><input type="radio" name="rot" value="2"> 2·朝上</label><label><input type="radio" name="rot" value="3"> 3·朝左</label></div>
<div class="row"><span class="lab">城市代码</span><input id="cc" type="text" maxlength="9" placeholder="9位数字，可留空"><a class="dim" href="http://www.weather.com.cn/forecast/" target="_blank">查询</a></div>
<div class="row"><span class="lab">城市名称</span><input id="cn" type="text" maxlength="8" placeholder="只填名称可自动查码"></div>
<div class="dim" style="margin:2px 0 6px">代码与名称填一个即可，都填以代码为准；名称不要带市、区、县等后缀，例如住北京市海淀区只需填“海淀”（显示名称≤4个汉字）</div>
<div class="row"><span class="lab">天气更新</span><input id="upt" type="number" min="1" max="60" value="10"><span class="dim">分钟（1-60）</span></div>
<div class="row"><button class="btn savebtn" id="btncfg" onclick="saveCfg()">保存设置</button></div></div>
<div class="card"><div class="t">分时段亮度</div>
<div class="row"><label style="display:flex;align-items:center;gap:6px"><input id="tbl" type="checkbox"> 启用：夜间自动降低亮度</label></div>
<div class="row"><span class="lab">夜间亮度</span><input id="nbl" type="range" min="1" max="100"><span id="nblv" style="width:40px"></span></div>
<div class="row"><span class="lab">夜间时段</span><span class="dim">从</span><select id="nst"></select><span class="dim">点 到次日</span><select id="net"></select><span class="dim">点</span></div>
<div class="dim" style="margin:4px 0 8px">夜间时段内自动切到“夜间亮度”，其余时间用上方“亮度·白天”；到点整小时切换</div>
<div class="row"><button class="btn savebtn" onclick="saveCfg()">保存设置</button></div></div>
<div class="card"><div class="t">固件升级</div>
<div class="row"><input id="fw" class="filein" type="file" accept=".bin"><button class="btn" id="btnota" onclick="doOta()">上传文件</button></div>
<div id="otabox" style="display:none;margin-top:4px"><div style="height:8px;background:#101318;border-radius:4px;overflow:hidden"><div id="otabar" style="height:100%;width:0;background:var(--acc)"></div></div><div id="otamsg" class="dim" style="margin-top:6px"></div></div></div>
<footer>SmallDesktopDisplay · 按 Ctrl+F5 强制刷新可更新页面</footer>
<div class="toast" id="tst"></div>
<script>
var tstT=null,rotLocked=false,editing=false,cityP=null,otaBusy=false,OTA_MAX=1040000;
function $(i){return document.getElementById(i)}
function tst(m){var t=$('tst');t.textContent=m;t.classList.add('show');clearTimeout(tstT);tstT=setTimeout(function(){t.classList.remove('show')},4000)}
/* 手动编辑过输入框后暂停自动回填，防止8秒刷新覆盖未保存内容 */
var fds=['bl','cc','cn','upt','tbl','nbl','nst','net'];for(var fi=0;fi<fds.length;fi++){(function(id){var e=$(id);e.oninput=e.onchange=function(){editing=true};})(fds[fi]);}
$('nbl').addEventListener('input',function(){$('nblv').textContent=this.value+'%'});
(function(){var o='';for(var i=0;i<24;i++)o+='<option value="'+i+'">'+i+'</option>';$('nst').innerHTML=o;$('net').innerHTML=o;})();
/* 城市名→城市代码：浏览器端加载weather.com.cn官方数据（服务端接口已失效且数据过大） */
function loadCityData(){
if(cityP)return cityP;
cityP=new Promise(function(res,rej){
var to=setTimeout(function(){cityP=null;rej(new Error('timeout'))},8000);
var sc=document.createElement('script');
sc.src='https://j.i8tq.com/weather2020/search/city.js';
sc.onload=function(){clearTimeout(to);window.city_data?res(window.city_data):(cityP=null,rej(new Error('nodata')))};
sc.onerror=function(){clearTimeout(to);cityP=null;rej(new Error('load'))};
document.head.appendChild(sc);
});
return cityP;}
function findCity(data,name){var r=cityWalk(data,name,0);if(!r.length){var alt=name.replace(/[省市州区旗]$/,'');if(alt&&alt!=name)r=cityWalk(data,alt,0);}if(!r.length&&name.length>=2)r=cityWalk(data,name,1);return r;}
function cityWalk(data,name,pre){var out=[];(function walk(o,p){for(var k in o){var v=o[k];if(v&&v.AREAID){var n=v.NAMECN||k;if(pre?n.indexOf(name)==0:n==name)out.push({id:v.AREAID,path:p.join(' ')});}else if(v&&typeof v=='object')walk(v,p.concat(k));}})(data,[]);return out;}
function getRot(){var r=document.querySelector('input[name=rot]:checked');return r?r.value:'0'}
function setRot(v){var r=document.querySelector('input[name=rot][value="'+v+'"]');if(r)r.checked=true}

/* 设备返回的字段级错误码 → 能让人照着改的中文 */
function errText(k){var m={bl:'亮度只能 1-100',upt:'天气更新间隔只能 1-60 分钟',tbl:'分时段亮度只接受 0/1',nbl:'夜间亮度只能 1-100',nst:'夜间开始只接受 0-23 点',net:'夜间结束只接受 0-23 点',cc:'城市代码要 7 位，例如 101250101',cn:'城市名称最多 24 个字',rot:'屏幕方向只能 0-3'};return m[k]||('参数不合法'+(k?'（'+k+'）':''))}
async function req(u,opt){var c=new AbortController();var to=setTimeout(function(){c.abort()},8000);opt=opt||{};opt.signal=c.signal;try{var r=await fetch(u,opt);clearTimeout(to);return await r.json()}catch(e){clearTimeout(to);return null}}
async function refresh(){
if(otaBusy)return;
var s=await req('/api/status');
if(!s){tst('设备无响应，请稍后重试');return}
var wxAge=(s.wxAge===4294967295)?'从未成功':(s.wxAge<120?s.wxAge+' 秒前':(s.wxAge<7200?Math.floor(s.wxAge/60)+' 分钟前':Math.floor(s.wxAge/3600)+' 小时前'));
var k=[['模式','内网STA已连接'],['IP地址',s.ip||'-'],['信号',s.rssi+' dBm'],['已连接SSID',s.ssid||'-'],['运行时长',Math.floor(s.up/3600)+'时'+Math.floor(s.up%3600/60)+'分'],['固件版本',s.ver],['天气更新',wxAge+(s.wxErr?'（上次HTTP '+s.wxErr+'）':'')],['天气读数',(s.temp||'-')+' / '+(s.hum||'-')],['白天/黑夜',(s.rise||'-')+' / '+(s.set||'-')],['可用内存',Math.round(s.heap/1024)+' KB（碎片 '+s.frag+'%）'],['Flash',s.flash+' KB，固件 '+s.sketch+' KB']];
var h='';for(var i=0;i<k.length;i++)h+='<div class="kv"><span>'+k[i][0]+'</span><span>'+k[i][1]+'</span></div>';
$('st').innerHTML=h;$('ver').textContent=s.ver;
if(!editing){$('bl').value=s.bl;$('blv').textContent=s.bl+'%';$('cc').value=s.city;$('cn').value=s.cn||'';$('upt').value=s.upt;
$('tbl').checked=s.tbl==1;$('nbl').value=s.nbl;$('nblv').textContent=s.nbl+'%';$('nst').value=s.nst;$('net').value=s.net;}
if(!rotLocked)setRot(s.rot);
}
function lockRot(){this.onchange=function(){rotLocked=true;tst('屏幕方向已改为 '+this.value+'，点击“保存设置”生效')}}
var rs=document.querySelectorAll('input[name=rot]');for(var i=0;i<rs.length;i++)rs[i].onclick=lockRot;
async function saveCfg(){
var bs=document.querySelectorAll('.savebtn');
function setBtn(t,en){for(var i=0;i<bs.length;i++){bs[i].disabled=en;bs[i].textContent=t;}}
setBtn('保存中…',true);
var cc=$('cc').value.trim(),cn=$('cn').value.trim();
if(!cc&&cn){
setBtn('查询城市码…',true);
var id='';
try{var m=findCity(await loadCityData(),cn);
if(m.length){id=m[0].id;$('cc').value=id;tst('已匹配：'+m[0].path+'（'+id+'）'+(m.length>1?'，同名'+m.length+'个，已取第一个，如有不符请手动改代码':''));}
else tst('未找到城市“'+cn+'”：请去掉市/区/县后缀重试，或手动填写城市代码');
}catch(e){tst('城市码自动查询失败（本页面需能访问外网），请手动填写城市代码');}
if(!id){setBtn('保存设置',false);return}
}
setBtn('保存中…',true);
var d=await req('/save',{method:'POST',body:new URLSearchParams({bl:$('bl').value,rot:getRot(),cc:$('cc').value,cn:cn,upt:$('upt').value,tbl:$('tbl').checked?1:0,nbl:$('nbl').value,nst:$('nst').value,net:$('net').value})});
setBtn('保存设置',false);
if(d&&d.ok==1){tst('设置已保存');rotLocked=false;editing=false;refresh()}else tst('保存失败：'+(d&&d.err?errText(d.err):'设备无响应'))}
/* 升级后设备要重启：轮询 /api/status 直到重新应答，再刷新页面（避免固定等待时间猜不准） */
function waitBack(n){
if(n>28){$('otamsg').textContent='等待超时，请手动刷新页面';return}
$('otamsg').textContent='等待重启'+(n?('（'+(n*3)+'s）'):'');
setTimeout(async function(){
var s=await req('/api/status');
if(s){$('otamsg').textContent='已升级到 '+s.ver;setTimeout(function(){location.reload()},1200)}
else waitBack(n+1)
},3000)}
/* 固件升级：multipart 上传到 /update，设备写完即重启，靠版本号确认 */
function doOta(){
var el=$('fw');
if(!el.files||!el.files.length){tst('先选一个 .bin 文件');return}
var f=el.files[0];
if(!/\.bin$/i.test(f.name)){tst('只支持 .bin 文件');return}
if(f.size>OTA_MAX){tst('文件太大，上限 '+(OTA_MAX/1024|0)+'KB');return}
if(!confirm('上传 '+f.name+'（'+(f.size/1024|0)+'KB）？\n上传期间勿断电。'))return;
var b=$('btnota'),box=$('otabox'),bar=$('otabar'),msg=$('otamsg');
otaBusy=true;b.disabled=true;b.textContent='上传中…';box.style.display='block';bar.style.width='0%';msg.textContent='上传中 0%';
var fd=new FormData();fd.append('firmware',f,f.name);
var x=new XMLHttpRequest();
x.open('POST','/update',true);
x.upload.onprogress=function(e){if(!e.lengthComputable)return;var p=Math.floor(e.loaded/e.total*100);bar.style.width=p+'%';msg.textContent='上传中 '+p+'%';};
x.onload=function(){
var j=null;try{j=JSON.parse(x.responseText)}catch(e2){}
if(j&&j.ok==1){bar.style.width='100%';waitBack(0)}
else{otaBusy=false;b.disabled=false;b.textContent='上传文件';msg.textContent='上传失败'+(j&&j.code?'（错误码 '+j.code+'）':'');}
};
x.onerror=function(){waitBack(0)};
x.send(fd);
}
window.onload=refresh;
setInterval(refresh,8000);
</script>
</body></html>)rawliteral";

#endif
