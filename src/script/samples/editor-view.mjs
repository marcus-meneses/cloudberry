/* Retained cells: allocate once, inspect only touched rows, batch presentation.
 * Graphics owns pixels/fonts; this class owns application UI damage only. */
export class RetainedGrid {
    constructor(cols, rows, cellWidth, cellHeight, graphics, fg, bg, bodyTop=0, bodyRows=rows) {
        Object.assign(this,{cols,rows,cellWidth,cellHeight,graphics});
        this.cells=Array.from({length:rows},()=>Array.from({length:cols},()=>[' ',fg,bg]));
        this.front=Array.from({length:rows},()=>Array.from({length:cols},()=>[null,0,0]));
        this.dirty=new Map();this.lines=new Map();this.frames=0;this.runs=0;this.bodyTop=bodyTop;this.bodyRows=bodyRows;this.layoutDirty=false;
        this.area=graphics.createSurface(cols*cellWidth,bodyRows*cellHeight,true);
        this.surfaces=Array.from({length:rows},()=>graphics.createSurface(cols*cellWidth,cellHeight));
        for(const id of this.surfaces)graphics.fillSurface(id,bg);
        graphics.placeSurfaces([{surface:this.area,parent:0n,x:0,y:bodyTop*cellHeight},...this.surfaces.map((surface,y)=>({surface,parent:y>=bodyTop&&y<bodyTop+bodyRows?this.area:0n,x:0,y:(y>=bodyTop&&y<bodyTop+bodyRows?y-bodyTop:y)*cellHeight}))]);
    }
    scrollBody(delta) {
        if(!delta||Math.abs(delta)>=this.bodyRows)return false;
        this.graphics.scrollSurface(this.area,0,-delta*this.cellHeight);
        for(const array of [this.cells,this.front,this.surfaces]){
            const part=array.slice(this.bodyTop,this.bodyTop+this.bodyRows),n=((delta%this.bodyRows)+this.bodyRows)%this.bodyRows;
            array.splice(this.bodyTop,this.bodyRows,...part.slice(n),...part.slice(0,n));
        }
        this.graphics.placeSurfaces(this.surfaces.slice(this.bodyTop,this.bodyTop+this.bodyRows).map((surface,y)=>({surface,parent:this.area,x:0,y:y*this.cellHeight})));
        this.layoutDirty=true;return true;
    }
    close() {for(const id of this.surfaces)this.graphics.releaseSurface(id);this.surfaces=[];this.graphics.releaseSurface(this.area);}
    begin(force=false) { this.dirty.clear();if(force)this.lines.clear(); }
    put(x,y,text,fg,bg) {
        if(y<0||y>=this.rows)return;
        for(const ch of text){if(x>=0&&x<this.cols){const c=this.cells[y][x];if(c[0]!==ch||c[1]!==fg||c[2]!==bg||this.front[y][x][0]===null){c[0]=ch;c[1]=fg;c[2]=bg;const range=this.dirty.get(y);if(range){range[0]=Math.min(range[0],x);range[1]=Math.max(range[1],x);}else this.dirty.set(y,[x,x]);}}x++;}
    }
    line(y,text,fg,bg) {
        const prior=this.lines.get(y);if(prior&&prior[0]===text&&prior[1]===fg&&prior[2]===bg)return;
        this.put(0,y,' '.repeat(this.cols),fg,bg);this.put(1,y,text,fg,bg);this.lines.set(y,[text,fg,bg]);
    }
    flush() {
        const commands=[];
        for(const [y,[lo,hi]] of this.dirty){let x=lo;while(x<=hi){const c=this.cells[y][x],p=this.front[y][x];if(c.every((v,i)=>v===p[i])){x++;continue;}const start=x,fg=c[1],bg=c[2];let text='';while(x<=hi){const c=this.cells[y][x],p=this.front[y][x];if(c[1]!==fg||c[2]!==bg||(x>start&&c.every((v,i)=>v===p[i])))break;text+=c[0];x++;}commands.push({x:start,y,text,fg,bg});}}
        if(!commands.length&&!this.layoutDirty)return 0;
        this.graphics.beginFrame();for(const c of commands)this.graphics.text(c.x*this.cellWidth,0,c.text,c.fg,c.bg,this.surfaces[c.y]);this.graphics.present();
        for(const [y,[lo,hi]] of this.dirty)for(let x=lo;x<=hi;x++){const c=this.cells[y][x],p=this.front[y][x];p[0]=c[0];p[1]=c[1];p[2]=c[2];}
        this.layoutDirty=false;this.frames++;this.runs+=commands.length;return commands.length;
    }
}
