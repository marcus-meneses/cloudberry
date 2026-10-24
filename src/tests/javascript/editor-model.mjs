import test from 'node:test';
import assert from 'node:assert/strict';
import {Document,decodeUTF8} from '../../script/samples/editor-model.mjs';
test('Unicode insertion, multiline edits and undo/redo preserve scalar boundaries',()=>{
    const d=new Document('a🍓b\nsecond');d.col=2;d.backspace();assert.equal(d.value(),'ab\nsecond');d.undo();assert.equal(d.value(),'a🍓b\nsecond');d.redo();assert.equal(d.value(),'ab\nsecond');
    d.insert('x\ny');assert.equal(d.value(),'ax\nyb\nsecond');assert.deepEqual([d.row,d.col],[1,1]);d.move('home');d.backspace();assert.equal(d.value(),'axyb\nsecond');d.delete();assert.equal(d.value(),'axb\nsecond');
});
test('dirty state, saved document, overwrite, clipboard and wrapped search',()=>{
    const d=new Document('hello\nworld');d.move('end');d.insert('!');assert.equal(d.dirty,true);d.savedAs('/a.txt');assert.equal(d.dirty,false);d.undo();assert.equal(d.dirty,true);d.redo();assert.equal(d.dirty,false);
    d.row=1;d.col=0;d.overwrite=true;d.insert('W');assert.equal(d.value(),'hello!\nWorld');d.copy();d.cut();assert.equal(d.value(),'hello!');d.row=0;d.col=0;d.overwrite=false;d.insert(d.clipboard);assert.equal(d.value(),'World\nhello!');assert.equal(d.find('World'),true);assert.deepEqual([d.row,d.col],[0,0]);
});
test('UTF-8 validates malformed data; BOM and CRLF survive a round trip',()=>{
    const text='\ufeffação 🍓\r\nnext\r\n';const decoded=decodeUTF8(new TextEncoder().encode(text));const d=new Document(decoded);assert.equal(d.value(),text);assert.equal(d.dirty,false);
    for(const bytes of [[0xc0,0xaf],[0xed,0xa0,0x80],[0xf4,0x90,0x80,0x80],[0xe2,0x82],[0x80]])assert.throws(()=>decodeUTF8(Uint8Array.from(bytes)),/UTF-8/);
});
test('delete joins lines, cut handles final empty document and navigation clamps',()=>{
    const d=new Document('one\ntwo');d.move('end');d.delete();assert.equal(d.value(),'onetwo');d.cut();assert.equal(d.value(),'');d.move('pageup');d.move('left');d.move('right');assert.deepEqual([d.row,d.col],[0,0]);d.undo();assert.equal(d.value(),'onetwo');
});

import {RetainedGrid} from '../../script/samples/editor-view.mjs';
import {FileBrowser,absolute,parent} from '../../script/samples/editor-files.mjs';
test('retained rendering batches changed cells and skips unchanged frames',()=>{
    const calls=[];let serial=0n;const graphics={createSurface(){return ++serial;},fillSurface(){},placeSurfaces(){},scrollSurface(){},releaseSurface(){},beginFrame(){calls.push('begin');},text(...args){calls.push(args);},present(){calls.push('present');}};
    const v=new RetainedGrid(10,4,8,18,graphics,1,0);v.begin();for(let y=0;y<4;y++)v.put(0,y,' '.repeat(10),1,0);assert.equal(v.flush(),4);assert.equal(calls.length,6);
    calls.length=0;v.begin();v.put(0,0,' '.repeat(10),1,0);assert.equal(v.flush(),0);assert.equal(calls.length,0);
    v.begin();v.put(2,2,'ç',1,0);assert.equal(v.flush(),1);assert.deepEqual(calls[1],[16,0,'ç',1,0,v.surfaces[2]]);
    calls.length=0;v.begin();v.put(9,3,'x',1,0);graphics.present=()=>{throw Error('transport');};assert.throws(()=>v.flush(),/transport/);assert.equal(v.front[3][9][0],' ');
});
test('file navigation sorts folders, filters hidden entries and preserves state on failure',()=>{
    const fs={stat(p){if(!['/','/docs','/empty'].includes(p))throw Error('unavailable');return {directory:true};},list(p){return p==='/'?[{name:'z.txt',directory:false},{name:'docs',directory:true},{name:'_private',directory:false},{name:'a.txt',directory:false}]:[];}};
    const b=new FileBrowser(fs,'/','/docs');assert.deepEqual(b.entries.map(e=>e.name),['docs','a.txt','z.txt']);b.toggleHidden();assert.equal(b.entries.length,4);b.select(30,2);assert.equal(b.top,2);b.working();assert.equal(b.directory,'/docs');assert.equal(b.selected,-1);assert.throws(()=>b.go('/missing'),/unavailable/);assert.equal(b.directory,'/docs');b.up();assert.equal(b.directory,'/');assert.equal(absolute('../z','/docs'),'/z');assert.equal(parent('/'),'/');assert.equal(b.path('docs/../a.txt'),'/a.txt');
});
test('editor line bitmaps move under an editing-area container without repainting unchanged text',()=>{
    let id=0n;const placed=[],moves=[],drawn=[],released=[];
    const graphics={createSurface(){return ++id;},fillSurface(){},placeSurfaces(rows){placed.push(rows);},scrollSurface(...args){moves.push(args);},releaseSurface(id){released.push(id);},beginFrame(){},text(...args){drawn.push(args);},present(){}};
    const v=new RetainedGrid(5,6,8,17,graphics,1,0,1,4);
    v.begin();for(let y=0;y<6;y++)v.put(0,y,('row'+y).padEnd(5),1,0);v.flush();const old=v.surfaces.slice(),before=drawn.length;
    assert.equal(v.scrollBody(1),true);assert.equal(v.surfaces[1],old[2]);assert.equal(v.surfaces[4],old[1]);assert.deepEqual(moves,[[v.area,0,-17]]);assert.equal(placed.at(-1).length,4);assert(placed.at(-1).every(row=>row.parent===v.area));
    v.begin();v.put(0,4,'new  ',1,0);assert.equal(v.flush(),2);assert.equal(drawn.length,before+2);assert(drawn.slice(before).every(run=>run[5]===old[1]));
    assert.equal(v.scrollBody(-1),true);assert.equal(v.surfaces[1],old[1]);v.close();assert.equal(released.length,7);
});
test('viewport scrolling preserves file caret coordinates, contents and edit target',()=>{
    const d=new Document(Array.from({length:100},(_,i)=>'line '+i).join('\n'));d.row=12;d.col=3;d.left=2;const saved=d.value();
    d.scroll(60);assert.equal(d.top,60);assert.deepEqual([d.row,d.col,d.left],[12,3,2]);assert.equal(d.value(),saved);assert.equal(d.dirty,false);
    d.scroll(-1000);assert.equal(d.top,0);d.scroll(1000);assert.equal(d.top,99);assert.deepEqual([d.row,d.col],[12,3]);d.insert('X');assert.equal(d.lines[12],'linXe 12');assert.equal(d.row,12);
});
