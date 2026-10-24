/* Cloudberry's graphical JavaScript text editor sample. UI policy lives here;
 * graphics, focus, device events and scoped files remain handle operations. */
import {system as os} from 'cloudberry';
import {Document, decodeUTF8} from './editor-model.mjs';
import {RetainedGrid} from './editor-view.mjs';
import {FileBrowser,absolute,parent,basename} from './editor-files.mjs';

const doc = new Document();
let gfx, keyboard, mouse, running = true, menu = -1, selected = 0, dialog = null;
let message = 'Ready', pending = null, view, bodyCache=[], previousCaret=null, lastDialog, lastMenu;
let cellWidth,cellHeight,lastTop=0,caretReveal=true;
let mx = 24, my = 24, wheel = 0, clickTargets = [];
const C = {text:0xe5edf5, dim:0x9eaebf, background:0x111923, bar:0x20334c,
           panel:0x263746, selected:0x29415d, accent:0x65dcac, cursor:0xd5ecfa};
const clamp = (n, lo, hi) => Math.max(lo, Math.min(hi, n));
const chars = s => Array.from(s);
const menus = [
    ['File', [['New','Ctrl+N',()=>request(()=>doc.reset(''))], ['Open...','Ctrl+O',()=>request(openDialog)],
              ['Save','Ctrl+S',()=>save(false)], ['Save as...','F4',()=>save(true)], ['Quit','Ctrl+Q',()=>request(()=>running=false)]]],
    ['Edit', [['Undo','Ctrl+Z',()=>doc.undo()], ['Redo','Ctrl+Y',()=>doc.redo()],
              ['Copy line','Ctrl+C',()=>doc.copy()], ['Cut line','Ctrl+X',()=>doc.cut()], ['Paste','Ctrl+V',()=>doc.insert(doc.clipboard)]]],
    ['Search', [['Find...','Ctrl+F',findDialog], ['Find next','F3',()=>findNext()]]],
    ['Help', [['About','F1',()=>notice('About','Cloudberry JS editor\nGraphics and input through handles.\nUTF-8 files; line clipboard; undo/redo.\nEsc closes a menu/dialog. Ctrl+Q quits.')]]]
];
let cols, rows, bodyRows, lastNeedle = '';
function request(action) {
    menu = -1;
    if (!doc.dirty) { action(); return; }
    pending = action;
    dialog = {title:'Unsaved changes', lines:['Save your changes before continuing?'], input:null, buttons:['Save','Discard','Cancel'], button:0,
        choose(index) { const action = pending; pending = null; dialog = null; if (index===0) save(false,action); else if (index===1) action(); }};
}
function notice(title, text) { menu=-1; dialog={title, lines:text.split('\n'), input:null, buttons:['OK'], button:0, choose(){dialog=null;}}; }
function ask(title, value, accept) { menu=-1; dialog={title, lines:[title==='Find text'?'Search wraps through the document.':'Path is relative to: '+os.cwd], input:value, cursor:chars(value).length, buttons:['OK','Cancel'], button:0,
    choose(index){const value=dialog.input;dialog=null;if(index===0)accept(value);}}; }
function openDialog(){fileDialog('open');}
function writeDocument(path, after=null) {try{os.writeFile(path,doc.value());doc.savedAs(path);message='Saved '+path;dialog=null;if(after)after();}catch(e){notice('Save failed',String(e)+'\nChanges remain unsaved.');}}
function save(as, after=null) {if(as||!doc.path)fileDialog('save',after);else writeDocument(doc.path,after);}
function fileDialog(mode, after=null) {
    menu=-1;
    try{const directory=doc.path?parent(absolute(doc.path,os.cwd)):os.cwd,browser=new FileBrowser(os,directory,os.cwd);
        dialog={type:'file',mode,title:mode==='open'?'Open file':'Save file',browser,input:doc.path?basename(doc.path):mode==='save'?'untitled.txt':'',cursor:0,selectAll:true,focus:'name',button:0,pathInput:browser.directory,pathCursor:browser.directory.length,pathSelectAll:false,error:'',after};dialog.cursor=chars(dialog.input).length;
    }catch(e){notice('Folder unavailable',String(e));}
}
function fileGo(d,path){try{d.browser.go(path);d.pathInput=d.browser.directory;d.pathCursor=chars(d.pathInput).length;d.error='';d.focus='files';if(d.mode==='open')d.input='';}catch(e){d.error='Cannot open folder: '+String(e);}}
function fileAccept(d,selected=false){
    if(selected){const entry=d.browser.entries[d.browser.selected];if(!entry)return;if(entry.directory){fileGo(d,d.browser.path(entry.name));return;}d.input=entry.name;d.cursor=chars(d.input).length;if(d.mode==='save'){d.focus='name';d.selectAll=true;return;}}
    if(!d.input){d.error='Enter a file name or select a file.';return;}
    const path=d.browser.path(d.input);let existing=null;try{existing=os.stat(path);}catch(e){if(e.status!==17){d.error=String(e);return;}}
    if(existing?.directory){fileGo(d,path);return;}
    if(d.newFolder){try{os.mkdir(path);d.newFolder=false;d.input=d.savedInput;d.cursor=chars(d.input).length;fileGo(d,path);}catch(e){d.error=String(e);}return;}
    if(d.mode==='open'){try{const data=decodeUTF8(os.readFile(path));doc.reset(data,path);message='Opened '+path;dialog=null;}catch(e){d.error='Open failed: '+String(e);}return;}
    if(existing){dialog={title:'Replace file?',lines:[path,'This file already exists. Replace it?'],input:null,buttons:['Replace','Cancel'],button:1,choose(index){if(index===0)writeDocument(path,d.after);else dialog=d;}};return;}
    writeDocument(path,d.after);
}
function newFolder(d){if(d.newFolder){d.newFolder=false;d.input=d.savedInput;}else {d.savedInput=d.input;d.newFolder=true;d.input='';}d.cursor=chars(d.input).length;d.focus='name';d.selectAll=true;d.error='';}
function editInput(d,e,press,ctrl){
    if(press&&ctrl&&e.usage===4){d.selectAll=true;return;}
    if(d.selectAll&&((e.kind==='text'&&!ctrl&&e.codepoint>=32)||(press&&(e.usage===42||e.usage===76)))){d.input='';d.cursor=0;d.selectAll=false;}
    const data=chars(d.input);
    if(press&&e.usage===80){d.cursor=Math.max(0,d.cursor-1);d.selectAll=false;}
    else if(press&&e.usage===79){d.cursor=Math.min(data.length,d.cursor+1);d.selectAll=false;}
    else if(press&&e.usage===74){d.cursor=0;d.selectAll=false;}
    else if(press&&e.usage===77){d.cursor=data.length;d.selectAll=false;}
    else if(press&&e.usage===42&&d.cursor){data.splice(--d.cursor,1);d.input=data.join('');}
    else if(press&&e.usage===76){data.splice(d.cursor,1);d.input=data.join('');}
    else if(e.kind==='text'&&!ctrl&&e.codepoint>=32){data.splice(d.cursor++,0,String.fromCodePoint(e.codepoint));d.input=data.join('');}
}
function fileKey(d,e,press,ctrl){
    if(press&&ctrl&&e.usage===15){d.focus='path';d.pathSelectAll=true;return;}
    if(press&&ctrl&&e.usage===11){d.browser.toggleHidden();return;}
    if(press&&e.usage===41){if(d.newFolder)newFolder(d);else dialog=null;return;}
    if(press&&(e.modifiers&0x44)&&e.usage===82){fileGo(d,parent(d.browser.directory));return;}
    if(press&&e.usage===43){const fields=['name','files','buttons','path'];d.focus=fields[(fields.indexOf(d.focus)+(e.modifiers&0x22?3:1))%4];return;}
    if(press&&e.usage===40){if(d.focus==='path')fileGo(d,d.pathInput);else if(d.focus==='buttons'&&d.button===1)dialog=null;else fileAccept(d,d.focus==='files');return;}
    if(d.focus==='files'){
        if(press&&e.usage===42){fileGo(d,parent(d.browser.directory));return;}
        const delta={82:-1,81:1,75:-d.page,78:d.page};if(press&&delta[e.usage]){d.browser.select(d.browser.selected+delta[e.usage],d.page);return;}
        if(press&&(e.usage===74||e.usage===77)){d.browser.select(e.usage===74?0:d.browser.entries.length-1,d.page);return;}
        if(e.kind!=='text'||ctrl||e.codepoint<32)return;d.focus='name';d.selectAll=true;
    }
    if(d.focus==='buttons'){if(press&&(e.usage===80||e.usage===79))d.button=1-d.button;return;}
    if(d.focus==='path'){const field={input:d.pathInput,cursor:d.pathCursor,selectAll:d.pathSelectAll};editInput(field,e,press,ctrl);d.pathInput=field.input;d.pathCursor=field.cursor;d.pathSelectAll=field.selectAll;}
    else editInput(d,e,press,ctrl);
}
function findDialog(){ask('Find text',lastNeedle,text=>{lastNeedle=text;findNext();});}
function findNext(){caretReveal=true;if(!lastNeedle){findDialog();return;}message=doc.find(lastNeedle)?'Found: '+lastNeedle:'Not found: '+lastNeedle;}
function action(index){const fn=menus[menu][1][index]?.[2];menu=-1;if(fn){caretReveal=true;fn();}}
function drawFileDialog(d,put,target,grid){
    clickTargets=[];const w=Math.min(cols-4,96),h=Math.min(rows-4,28),left=Math.floor((cols-w)/2),top=Math.floor((rows-h)/2),page=h-10;d.page=page;
    for(let y=0;y<h;y++)put(left,top+y,' '.repeat(w),C.text,C.panel);
    put(left,top,'+'+'-'.repeat(w-2)+'+',C.accent,C.panel);put(left+2,top,d.title,C.accent,C.panel);
    const buttons=[['Up',()=>fileGo(d,parent(d.browser.directory))],['Working dir',()=>fileGo(d,os.cwd)],['New folder',()=>newFolder(d)],['Hidden: '+(d.browser.hidden?'on':'off'),()=>d.browser.toggleHidden()]];let bx=left+2;
    for(const [label,fn] of buttons){const text='[ '+label+' ]';put(bx,top+1,text,C.text,C.selected);target(bx,top+1,text.length,1,fn);bx+=text.length+1;}
    function field(x,y,label,value,cursor,active,all,available){put(x,y,label,C.dim,C.panel);x+=label.length;put(x,y,' '.repeat(available),C.text,C.background);const data=chars(value),start=active?Math.max(0,cursor-available+1):Math.max(0,data.length-available);put(x,y,data.slice(start,start+available).join(''),C.text,active&&all?C.selected:C.background);if(active){const cx=x+cursor-start;if(cx>=x&&cx<x+available)put(cx,y,grid[y][cx][0],C.background,C.cursor);}}
    field(left+2,top+3,'Path: ',d.pathInput,d.pathCursor,d.focus==='path',d.pathSelectAll,w-10);target(left+2,top+3,w-4,1,()=>{d.focus='path';d.pathSelectAll=true;});
    put(left+2,top+4,'Folders and files | Enter opens folder/file; Backspace goes up',C.dim,C.panel);
    for(let n=0;n<page;n++){const index=d.browser.top+n,entry=d.browser.entries[index],y=top+5+n,bg=index===d.browser.selected?C.selected:C.background;put(left+2,y,' '.repeat(w-4),C.text,bg);if(entry){put(left+3,y,(entry.directory?'[D] ':'    ')+chars(entry.name).slice(0,w-10).join(''),C.text,bg);target(left+2,y,w-4,1,e=>{const repeated=d.lastClick===index&&d.lastDir===d.browser.directory&&e.timestampNs-d.lastTime<500000000n;d.browser.selected=index;d.focus='files';if(!entry.directory){d.input=entry.name;d.cursor=chars(d.input).length;}if(repeated){fileAccept(d,true);d.lastClick=-1;}else{d.lastClick=index;d.lastDir=d.browser.directory;d.lastTime=e.timestampNs;}});}}
    field(left+2,top+h-4,d.newFolder?'Folder: ':'Name: ',d.input,d.cursor,d.focus==='name',d.selectAll,w-12);target(left+2,top+h-4,w-4,1,()=>{d.focus='name';d.selectAll=true;});
    put(left+2,top+h-3,chars(d.error||`${d.browser.entries.length} entries | Ctrl+L path, Ctrl+H hidden, Tab focus`).slice(0,w-4).join(''),d.error?0xffa090:C.dim,C.panel);
    const ok=d.newFolder?'Create folder':d.mode==='open'?'Open':'Save';bx=left+2;[ok,'Cancel'].forEach((label,i)=>{const text='[ '+label+' ]';put(bx,top+h-2,text,C.text,d.focus==='buttons'&&d.button===i?C.selected:C.bar);target(bx,top+h-2,text.length,1,()=>{if(i===1)dialog=null;else fileAccept(d);});bx+=text.length+2;});
}
function render() {
    let hidden=false;try{if(mouse){mouse.hideCursor();hidden=true;}renderFrame();}finally{if(hidden)mouse.showCursor();}
}
function renderFrame() {
    const gutter=Math.max(5,String(doc.lines.length).length+1), width=cols-gutter;
    doc.top=clamp(doc.top,0,Math.max(0,doc.lines.length-1));
    if(caretReveal && !dialog && menu<0){if(doc.row<doc.top)doc.top=doc.row;if(doc.row>=doc.top+bodyRows)doc.top=doc.row-bodyRows+1;doc.left=Math.max(0,Math.min(doc.left,doc.col));if(doc.col>=doc.left+width)doc.left=doc.col-width+1;}
    const force=lastDialog!==dialog||lastMenu!==menu,shift=doc.top-lastTop;
    if(!force && !dialog && menu<0 && shift && Math.abs(shift)<bodyRows){
        view.scrollBody(shift);const n=((shift%bodyRows)+bodyRows)%bodyRows;
        bodyCache=[...bodyCache.slice(n),...bodyCache.slice(0,n)];
        if(previousCaret){previousCaret=[previousCaret[0]-shift,previousCaret[1]];if(previousCaret[0]<3||previousCaret[0]>=rows-2)previousCaret=null;}
    }
    view.begin(force);const grid=view.cells;
    if(force)bodyCache=[];
    function put(x,y,text,fg=C.text,bg=C.background){view.put(x,y,text,fg,bg);}
    function line(y,text,fg=C.text,bg=C.bar){view.line(y,text,fg,bg);}
    function target(x,y,w,h,fn){clickTargets.push({x,y,w,h,fn});}
    clickTargets=[];
    line(0,'Cloudberry JS editor | '+(doc.path || 'Untitled')+(doc.dirty?' *':''),C.accent);
    line(1,'');let x=1;
    menus.forEach(([name],i)=>{const label=' '+name+' ';put(x,1,label,C.text,menu===i?C.selected:C.bar);target(x,1,label.length,1,()=>{menu=menu===i?-1:i;selected=0;});x+=label.length+2;});
    line(2,'Ctrl+O Open   Ctrl+S Save   Ctrl+F Find   F10 Menus   Ctrl+Q Quit',C.dim,C.background);
    const caret=[doc.row-doc.top+3,gutter+doc.col-doc.left];
    for(let y=0;y<bodyRows;y++){const index=doc.top+y,text=doc.lines[index]??'',prior=bodyCache[y];const restore=previousCaret&&previousCaret[0]===y+3&&(force||previousCaret[0]!==caret[0]||previousCaret[1]!==caret[1]);
        if(!prior||prior.text!==text||prior.index!==index||prior.left!==doc.left||prior.width!==width||restore){put(0,y+3,' '.repeat(cols));if(index<doc.lines.length){put(0,y+3,String(index+1).padStart(gutter-1)+' ',C.dim);put(gutter,y+3,chars(text).slice(doc.left,doc.left+width).map(c=>c==='\t'?' ':c).join(''));}bodyCache[y]={text,index,left:doc.left,width};}}
    line(rows-2,`${doc.dirty?'Modified':'Saved'} | Ln ${doc.row+1}, Col ${doc.col+1} | ${doc.overwrite?'OVR':'INS'} | ${doc.eol==='\r\n'?'CRLF':'LF'} | ${message}`,C.accent);
    line(rows-1,'UTF-8 | Mouse: menus/caret, wheel: scroll | Copy/Cut operate on the current line',C.dim);
    const caretVisible=!dialog && menu<0 && caret[0]>=3 && caret[0]<rows-2 && caret[1]>=gutter && caret[1]<cols;
    if(caretVisible)put(caret[1],caret[0],grid[caret[0]][caret[1]][0],C.background,C.cursor);
    if(menu>=0){let left=1;for(let i=0;i<menu;i++)left+=menus[i][0].length+4;const items=menus[menu][1],w=30;for(let i=0;i<items.length;i++){const bg=selected===i?C.selected:C.panel;put(left,i+2,' '.repeat(w),C.text,bg);put(left+1,i+2,items[i][0],C.text,bg);put(left+w-items[i][1].length-1,i+2,items[i][1],C.dim,bg);target(left,i+2,w,1,()=>action(i));}}
    if(dialog?.type==='file'){drawFileDialog(dialog,put,target,grid);}else if(dialog){clickTargets=[];const w=Math.min(cols-4,76),h=Math.max(8,dialog.lines.length+6),left=Math.floor((cols-w)/2),top=Math.floor((rows-h)/2);
        for(let y=0;y<h;y++)put(left,top+y,' '.repeat(w),C.text,C.panel);
        put(left,top,'+'+'-'.repeat(w-2)+'+',C.accent,C.panel);put(left+2,top,dialog.title,C.accent,C.panel);
        dialog.lines.forEach((text,i)=>put(left+2,top+2+i,chars(text).slice(0,w-4).join(''),C.text,C.panel));
        if(dialog.input!==null){const text=chars(dialog.input),available=w-5,start=Math.max(0,dialog.cursor-available+1),y=top+h-4;put(left+2,y,' '.repeat(w-4),C.text,C.background);put(left+2,y,text.slice(start,start+available).join(''),C.text,dialog.selectAll?C.selected:C.background);const cx=left+2+dialog.cursor-start;put(cx,y,grid[y][cx][0],C.background,C.cursor);}
        let bx=left+2;dialog.buttons.forEach((button,i)=>{const text='[ '+button+' ]',bg=dialog.button===i?C.selected:C.panel;put(bx,top+h-2,text,C.text,bg);target(bx,top+h-2,text.length,1,()=>dialog.choose(i));bx+=text.length+2;});
    }
    view.flush();lastTop=doc.top;lastDialog=dialog;lastMenu=menu;previousCaret=caretVisible?caret:null;caretReveal=false;
}

function key(e) {
    if(e.kind==='reset'){menu=-1;message='Input resynchronized';return;}
    const press=e.kind==='down'||e.kind==='repeat', ctrl=(e.modifiers&0x11)!==0;
    if(dialog?.type==='file'){fileKey(dialog,e,press,ctrl);return;}
    if(dialog){
        if(press&&ctrl&&e.usage===4&&dialog.input!==null){dialog.selectAll=true;return;}
        if(press&&e.usage===41){dialog=null;pending=null;return;}
        if(press&&e.usage===43){dialog.button=(dialog.button+1)%dialog.buttons.length;return;}
        if(press&&e.usage===40){dialog.choose(dialog.button);return;}
        if(dialog.input!==null){if(dialog.selectAll&&((e.kind==='text'&&!ctrl&&e.codepoint>=32)||(press&&(e.usage===42||e.usage===76)))){dialog.input='';dialog.cursor=0;dialog.selectAll=false;}const data=chars(dialog.input);if(press&&e.usage===80)dialog.cursor=Math.max(0,dialog.cursor-1);else if(press&&e.usage===79)dialog.cursor=Math.min(data.length,dialog.cursor+1);else if(press&&e.usage===74)dialog.cursor=0;else if(press&&e.usage===77)dialog.cursor=data.length;else if(press&&e.usage===42&&dialog.cursor){data.splice(--dialog.cursor,1);dialog.input=data.join('');}else if(press&&e.usage===76){data.splice(dialog.cursor,1);dialog.input=data.join('');}else if(e.kind==='text'&&!ctrl&&e.codepoint>=32){data.splice(dialog.cursor++,0,String.fromCodePoint(e.codepoint));dialog.input=data.join('');}}
        return;
    }
    if(press&&e.usage===67){menu=menu<0?0:-1;selected=0;return;}
    if(menu>=0){if(!press)return;if(e.usage===41)menu=-1;else if(e.usage===80){menu=(menu+menus.length-1)%menus.length;selected=0;}else if(e.usage===79){menu=(menu+1)%menus.length;selected=0;}else if(e.usage===82)selected=(selected+menus[menu][1].length-1)%menus[menu][1].length;else if(e.usage===81)selected=(selected+1)%menus[menu][1].length;else if(e.usage===40)action(selected);return;}
    if(press&&ctrl){const commands={17:()=>request(()=>doc.reset('')),18:()=>request(openDialog),22:()=>save(false),20:()=>request(()=>running=false),9:findDialog,29:()=>doc.undo(),28:()=>doc.redo(),6:()=>doc.copy(),27:()=>doc.cut(),25:()=>doc.insert(doc.clipboard)};if(commands[e.usage]){caretReveal=true;commands[e.usage]();}return;}
    if(press&&(e.usage===75||e.usage===78)){doc.scroll(e.usage===75?-bodyRows:bodyRows);caretReveal=false;return;}
    if(press){caretReveal=true;const moves={80:'left',79:'right',82:'up',81:'down',74:'home',77:'end'};if(moves[e.usage])doc.move(moves[e.usage],bodyRows);else if(e.usage===40)doc.insert('\n');else if(e.usage===42)doc.backspace();else if(e.usage===76)doc.delete();else if(e.usage===43)doc.insert('    ');else if(e.usage===73)doc.overwrite=!doc.overwrite;else if(e.usage===58)menus[3][1][0][2]();else if(e.usage===60)findNext();else if(e.usage===61)save(true);}
    else if(e.kind==='text'&&!ctrl&&e.codepoint>=32&&e.codepoint!==127){caretReveal=true;doc.insert(String.fromCodePoint(e.codepoint));}
}
function readInput(device, source) { return device.next().then(events=>({source,events}), error=>{error.message=source+' input failed (status '+error.status+')';throw error;}); }
function pointer(e) {
    if(e.kind!=='input')return false;mx=e.cursorX;my=e.cursorY;let changed=false;
    if(e.wheelY){wheel+=Number(e.wheelY)/Math.max(1,e.resolutionY);const amount=Math.trunc(wheel);wheel-=amount;if(amount){changed=true;if(dialog?.type==='file')dialog.browser.top=clamp(dialog.browser.top-amount*3,0,Math.max(0,dialog.browser.entries.length-dialog.page));else if(!dialog){doc.scroll(-amount*3);caretReveal=false;}}}
    if((e.changed&1n)&&(e.buttons&1n)){const x=Math.floor(mx/cellWidth),y=Math.floor(my/cellHeight);for(let i=clickTargets.length-1;i>=0;i--){const t=clickTargets[i];if(x>=t.x&&x<t.x+t.w&&y>=t.y&&y<t.y+t.h){t.fn(e);return true;}}
        if(!dialog){menu=-1;if(y>=3&&y<rows-2){caretReveal=true;doc.row=clamp(doc.top+y-3,0,doc.lines.length-1);const gutter=Math.max(5,String(doc.lines.length).length+1);doc.col=clamp(doc.left+x-gutter,0,doc.length());}return true;}}
    return changed;
}

try {
    gfx=os.graphics();cellWidth=gfx.cellWidth;cellHeight=gfx.cellHeight;cols=Math.floor(gfx.width/cellWidth);rows=Math.floor(gfx.height/cellHeight);bodyRows=rows-5;view=new RetainedGrid(cols,rows,cellWidth,cellHeight,gfx,C.text,C.background,3,bodyRows);
    if(cols<60||rows<20)throw Error(`Editor needs at least ${60*cellWidth}x${20*cellHeight} pixels`);
    keyboard=os.keyboard();try{mouse=os.mouse();}catch(e){if(e.status!==2&&e.status!==17)throw e;message='Mouse unavailable; keyboard controls active';}mx=24;my=24;
    if(os.args[1]){try{doc.reset(decodeUTF8(os.readFile(os.args[1])),os.args[1]);}catch(e){notice('Open failed',String(e));}}
    gfx.fill(0,0,gfx.width,gfx.height,C.background);render();
    let keys=readInput(keyboard,'key'), moves=mouse?readInput(mouse,'mouse'):new Promise(()=>{});
    while(running){const batch=await Promise.race([keys,moves]);let repaint=false;for(const event of batch.events){if(batch.source==='key'){if(event.kind==='text'||event.kind==='reset'||((event.kind==='down'||event.kind==='repeat')&&((event.modifiers&0x11)||dialog||menu>=0||[40,42,43,58,60,61,67,73,74,75,76,77,78,79,80,81,82].includes(event.usage)))){key(event);repaint=true;}}else repaint=pointer(event)||repaint;if(!running)break;}if(running){if(repaint)render();if(batch.source==='key')keys=readInput(keyboard,'key');else moves=mouse?readInput(mouse,'mouse'):new Promise(()=>{});}}
} finally {
    try{if(view)view.close();if(gfx){gfx.fill(0,0,gfx.width,gfx.height,0);gfx.present();}}
    finally{try{if(mouse)mouse.close();}finally{try{if(keyboard)keyboard.close();}finally{if(gfx)gfx.close();}}}
}
