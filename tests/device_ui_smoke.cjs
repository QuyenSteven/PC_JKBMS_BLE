'use strict';
const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const html=fs.readFileSync('web/index.html','utf8');
const js=fs.readFileSync('web/device.js','utf8');
new vm.Script(js,{filename:'web/device.js'});
for(const id of ['device-admin','device-native-name','device-native-model','device-native-serial','device-local-name','device-local-save','device-local-reset']){
 assert.ok(html.includes('id="'+id+'"'),'Device manager control missing: '+id);
}
class FakeElement{
 constructor(){this.value='';this.textContent='';this.disabled=false;this.events={};}
 addEventListener(name,cb){this.events[name]=cb;}
 click(){this.events.click?.();}
}
const nodes=new Map(),store=new Map(),callbacks=[];
const el=id=>{if(!nodes.has(id))nodes.set(id,new FakeElement());return nodes.get(id);};
const window={localStorage:{getItem:k=>store.get(k)||null,setItem:(k,v)=>store.set(k,v),removeItem:k=>store.delete(k)},
 chrome:{webview:{addEventListener:(type,cb)=>callbacks.push(cb)}}};
const document={getElementById:el};
vm.runInNewContext(js,{window,document,console,String,Map});
el('device-list').value='43525665';
el('device-list').selectedOptions=[{textContent:'JK_BMS [A0B1C2D3]'}];
el('device-list').events.change();
el('device-local-name').value='Solar_8S';
el('device-local-save').click();
assert.equal(store.get('jk-bms-studio.alias.43525665'),'Solar_8S');
assert.equal(el('device-label').textContent,'Solar_8S');
callbacks.forEach(cb=>cb({data:{type:'deviceInfo',deviceName:'JK-8S-REAL',model:'JK-B2A8S20P',
 firmware:'10.1',serial:'ABC123456789'}}));
assert.equal(el('device-native-name').textContent,'JK-8S-REAL');
assert.equal(el('device-native-serial').textContent,'ABC123456789');
assert.equal(el('device-label').textContent,'Solar_8S');
el('device-local-reset').click();
assert.equal(store.has('jk-bms-studio.alias.43525665'),false);
assert.equal(el('device-label').textContent,'JK-8S-REAL');
assert.ok(html.includes('jkbms-ess.com/pages/support'));
console.log('JK device alias, identity & official recovery links: PASS');
