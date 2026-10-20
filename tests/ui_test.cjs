"use strict";
// Exercise the real rendering function with a small DOM contract double.
const fs=require("node:fs"),vm=require("node:vm"),assert=require("node:assert/strict");
class Element {
  constructor(tag){this.tag=tag;this.children=[];this.dataset={};this.open=false;this.classList={toggle(){}};}
  append(...nodes){this.children.push(...nodes);}
  replaceChildren(...nodes){this.children=nodes;}
  addEventListener(){}
  querySelectorAll(){return this.children.flatMap(n=>[...(n.tag==='details'?[n]:[]),...n.querySelectorAll()]);}
}
const nodes=new Map();
const document={getElementById(id){if(!nodes.has(id))nodes.set(id,new Element('div'));return nodes.get(id);},
  querySelectorAll(){return [];},createElement(tag){return new Element(tag);}};
const context=vm.createContext({document,console,setInterval(){},fetch:()=>new Promise(()=>{})});
vm.runInContext(fs.readFileSync('startech_raid/web/ui.js','utf8'),context);
const sample={query_ok:true,ssds:[0,1].map(port=>({port,identity_verified:true,temperature_candidate_c:35,smart_data_valid:true,attributes:[{id:5,label:'Reallocated sectors',raw:0}]})),known_ssds:[]};
context.sample=sample;
const render=()=>vm.runInContext('drives(sample)',context);
const panels=()=>nodes.get('drive-cards').querySelectorAll();
render();assert.deepEqual(panels().map(p=>p.open),[false,false]);
panels()[0].open=true;render();assert.deepEqual(panels().map(p=>p.open),[true,false]);
panels()[1].open=true;render();assert.deepEqual(panels().map(p=>p.open),[true,true]);
panels()[0].open=false;sample.query_ok=false;render();assert.deepEqual(panels().map(p=>p.open),[false,true]);
sample.query_ok=true;render();render();assert.deepEqual(panels().map(p=>p.open),[false,true]);
assert.ok(nodes.get('drive-cards').children[0].children.at(-1).children[1].children[0].children[1].children.some(c=>c.textContent==='Reallocated sectors'));
console.log('SMART expansion: independent ports, repeated refresh, user collapse and unavailable/recovery passed');
