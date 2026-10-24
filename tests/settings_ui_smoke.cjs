'use strict';
const fs=require('fs'),vm=require('vm'),assert=require('assert/strict');
const content=fs.readFileSync('web/settings.js','utf8');
new vm.Script(content,{filename:'web/settings.js'});
class FakeElement{
 constructor(id=''){this.id=id;this.value='';this.textContent='';this.children=[];this.style={};this.dataset={};
 this.disabled=false;this.events={};this.className='';
 this.classList={add(){},remove(){},toggle(){}};
 this.querySelector=()=>new FakeElement('row-status');
 }
 set innerHTML(v){this.html=v;this.children=[];}
 get innerHTML(){return this.html||'';}
 addEventListener(type,fn){this.events[type]=fn;}
 appendChild(item){this.children.push(item);return item;}
 append(...items){this.children.push(...items);}
 replaceChildren(...items){this.children=items;}
 click(){this.events.click?.();}
 remove(){}
}
const controls=new Map();
const get=id=>{if(!controls.has(id))controls.set(id,new FakeElement(id));return controls.get(id);};
const commands=[],callbacks=[];
const document={
 getElementById:get,createElement:tag=>new FakeElement(tag),
 createDocumentFragment:()=>new FakeElement('fragment'),
 body:{appendChild(){}}
};
const window={confirm:()=>true,prompt:()=> 'GHI',
 chrome:{webview:{postMessage:cmd=>commands.push(cmd),addEventListener:(n,fn)=>callbacks.push(fn)}},
};
const ctx=vm.createContext({window,document,console,Date,Number,Math,Map,Array,JSON,Blob:class{},URL:{createObjectURL(){return 'blob:sample';},revokeObjectURL(){}}});
vm.runInContext(content,ctx);
assert.ok(window.JkSettingsUI);
const emit=obj=>callbacks.forEach(fn=>fn({data:obj}));
emit({type:'status',message:'connected'});
const values=[
 {id:'cell_uvp',group:'Điện áp',label:'Cell UVP',unit:'V',value:2.90,precision:3,min:1.2,max:4.35,writable:true},
 {id:'cell_ovp',group:'Điện áp',label:'Cell OVP',unit:'V',value:3.65,precision:3,min:1.2,max:4.35,writable:true},
 {id:'cell_count',group:'Pin',label:'Cell count',unit:'',value:8,precision:0,min:2,max:32,writable:false}
];
emit({type:'settings',model:'JK-8S',firmware:'11.0',protocol:'JK02_32S',ready:true,fields:values});
assert.equal(window.JkSettingsUI.getCurrent().size,3);
assert.equal(get('settings-frame-state').textContent,'3 mục');
assert.equal(get('settings-apply').disabled,true);
window.JkSettingsUI.stage('cell_uvp',2.95);
assert.equal(window.JkSettingsUI.getDirty().length,1);
get('settings-apply').click();
assert.equal(commands.at(-1),'settings:write:cell_uvp:2.95');
emit({type:'settingWrite',key:'cell_uvp',state:'pending',message:'Readback first'});
emit({type:'settingWrite',key:'cell_uvp',state:'verified',message:'Verified from BMS'});
assert.equal(window.JkSettingsUI.getDirty().length,0);
window.JkSettingsUI.stage('cell_ovp',3.7);
emit({type:'settingsClear'});
assert.equal(window.JkSettingsUI.getCurrent(),null);
assert.equal(get('settings-apply').disabled,true);
assert.equal(window.JkSettingsUI.getDirty().length,0);
console.log('Settings WebUI: snapshot, guarded writes, readback state, disconnect reset PASS');
