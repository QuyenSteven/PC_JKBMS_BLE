'use strict';
const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const html=fs.readFileSync('web/index.html','utf8');
assert.doesNotMatch(html,/class="footnote"|id="safety"|Web UI prototype 0\.1/);
for(const id of ['cells','cell-grid-page','cell-bars','ctrl-charge','ctrl-discharge','ctrl-balance',
 'protection-status','settings-frame-state','ble-log','frame-settings','frame-telemetry','frame-info',
 'overview-temp1','overview-temp2','overview-temp-mos','overview-temp3','overview-temp4','overview-temp5',
 'device-native-name','device-local-name']){
 assert.ok(html.includes('id="'+id+'"'),'Missing '+id);
}
const match=html.match(/<script>([\s\S]*?)<\/script>/);
assert.ok(match,'Missing inline dashboard JS');
new vm.Script(match[1],{filename:'web/index.html:script'});

class FakeElement{
 constructor(id=''){this.id=id;this.textContent='';this.value='';this.style={};this.children=[];this.events={};this.attributes={};this.activeClasses=new Set();
  this.classList={add:(...v)=>v.forEach(k=>this.activeClasses.add(k)),remove:(...v)=>v.forEach(k=>this.activeClasses.delete(k)),
  toggle:(key,on)=>{if(on===undefined)on=!this.activeClasses.has(key);if(on)this.activeClasses.add(key);else this.activeClasses.delete(key);return on;},
  contains:key=>this.activeClasses.has(key)};
 }
 set innerHTML(v){this._html=v;this.children=[];}
 get innerHTML(){return this._html||'';}
 addEventListener(k,handler){this.events[k]=handler;}
 append(...nodes){for(const n of nodes){if(n?.fragment)this.children.push(...n.children);else this.children.push(n);}}
 appendChild(n){this.append(n);return n;}
 replaceChildren(...nodes){this.children=[];this.append(...nodes);}
 closest(){return this;} focus(){} select(){} scrollTop=0;scrollHeight=0;
}
const elements=new Map(),messages=[],navigation=[new FakeElement('nav-overview'),new FakeElement('nav-cells')],views=[new FakeElement('overview'),new FakeElement('cells')];
const element=id=>{if(!elements.has(id))elements.set(id,new FakeElement(id));return elements.get(id);};
element('side-dot').parentElement=new FakeElement('sidebar-connection');
for(const nav of navigation)nav.dataset={page:nav.id.slice(4)};
const document={
 getElementById:element,
 querySelectorAll:(q)=>q==='[data-page]'?navigation:q==='.view'?views:[],
 createElement:tag=>new FakeElement(tag),
 createDocumentFragment:()=>{const f=new FakeElement('fragment');f.fragment=true;return f;},
 createTextNode:txt=>({textContent:txt}),
 execCommand:()=>true
};
const window={chrome:{webview:{addEventListener(){},postMessage:msg=>messages.push(msg)}}};
const ctx=vm.createContext({document,window,console,Date,Number,Math,String,Array,Map});
vm.runInContext(match[1],ctx);
const emit=(data)=>vm.runInContext('receive({data:'+JSON.stringify(data)+'})',ctx);
emit({type:'status',message:'connected'});
assert.match(element('connection-status').innerHTML,/Đã kết nối/);
emit({type:'telemetry',protocol:'JK02_32S',packVerified:true,voltage:26.77,current:2.49,power:66.6573,soc:100,
 charge:1,discharge:0,balance:1,temperature1:25.3,temperature2:30.1,mosTemperature:42.5,temperature3:31.0,
 cells:[3.348,3.348,3.345,3.345,3.348,3.345,3.345,3.345]});
assert.equal(element('overview-temp1').textContent,'25.3 °C');
assert.equal(element('overview-temp2').textContent,'30.1 °C');
assert.equal(element('overview-temp-mos').textContent,'42.5 °C');
assert.equal(element('overview-temp3').textContent,'31.0 °C');
assert.equal(element('extra-temperatures').hidden,true); // T5 only; always show T3/T4
assert.ok(html.includes('<div class="temperature-icon">T3</div>'));
assert.ok(html.includes('<div class="temperature-icon">T4</div>'));
assert.ok(html.indexOf('overview-temp3') < html.indexOf('id="extra-temperatures"'));
assert.ok(html.indexOf('overview-temp4') < html.indexOf('id="extra-temperatures"'));
emit({type:'telemetry',protocol:'JK02_32S',packVerified:true,voltage:26.77,current:0,power:0,soc:100,
 charge:1,discharge:0,balance:0,temperature1:23.1,temperature2:23.2,mosTemperature:28.4,temperature3:null,temperature4:null,temperature5:null,
 cells:[3.348,3.348,3.345,3.345,3.348,3.345,3.345,3.345]});
assert.equal(element('overview-temp3').textContent,'-- °C');
assert.equal(element('overview-temp4').textContent,'-- °C');
assert.equal(element('extra-temperatures').hidden,true);
emit({type:'telemetry',protocol:'JK02_32S',packVerified:true,voltage:26.77,current:0,power:0,soc:100,
 charge:1,discharge:0,balance:0,temperature1:23.1,temperature2:23.2,mosTemperature:28.4,temperature3:24.3,temperature4:25.4,temperature5:26.5,
 cells:[3.348,3.348,3.345,3.345,3.348,3.345,3.345,3.345]});
assert.equal(element('overview-temp4').textContent,'25.4 °C');
assert.equal(element('extra-temperatures').hidden,false);
assert.equal(element('cell-grid-page').children.length,8);
assert.equal(element('cell-bars').children.length,8);
assert.equal(element('cells-count').textContent,'8');
assert.equal(element('cells-delta').textContent,'3 mV');
assert.equal(element('ctrl-charge').textContent,'BẬT');
assert.equal(element('ctrl-discharge').textContent,'TẮT');
assert.equal(element('pack-soc').children[0].textContent,'100');
emit({type:'frame',message:'0x01 (300 bytes)'});
assert.match(element('protection-status').textContent,/Đã nhận Settings RAW/);
emit({type:'status',message:'disconnected'});
assert.equal(element('cells-count').textContent,'--');
assert.equal(element('ctrl-charge').textContent,'--');
assert.equal(element('device-status').textContent,'OFFLINE');
assert.equal(element('overview-temp1').textContent,'-- °C');
assert.equal(element('extra-temperatures').hidden,true);
console.log('WebUI smoke test: dynamic cells, chart, BLE states, protection and reset PASS');
