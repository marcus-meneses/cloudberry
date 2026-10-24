/* Scope is enforced by the OS filesystem handle, never by these lexical helpers. */
export function absolute(path, base='/') {
    const result=[];for(const part of ((path.startsWith('/')?'':base+'/')+path).split('/')){if(!part||part==='.')continue;if(part==='..')result.pop();else result.push(part);}return '/'+result.join('/');
}
export function parent(path){const p=absolute(path);return p.slice(0,p.lastIndexOf('/'))||'/';}
export function basename(path){return path.slice(path.lastIndexOf('/')+1);}
export class FileBrowser {
    constructor(fs, directory='/', workingDirectory='/') {this.fs=fs;this.workingDirectory=workingDirectory;this.hidden=false;this.top=0;this.selected=-1;this.go(directory);}
    go(path) {
        const directory=absolute(path,this.directory||this.workingDirectory);if(!this.fs.stat(directory).directory)throw Error('Choose a folder');
        const all=this.fs.list(directory).filter(e=>e.name!=='.'&&e.name!=='..').sort((a,b)=>a.directory!==b.directory?(a.directory?-1:1):a.name.toLowerCase()<b.name.toLowerCase()?-1:a.name.toLowerCase()>b.name.toLowerCase()?1:0);
        this.directory=directory;this.all=all;this.applyFilter();
    }
    applyFilter(){this.entries=this.all.filter(e=>this.hidden||!e.name.startsWith('_'));this.top=0;this.selected=this.entries.length?0:-1;}
    toggleHidden(){this.hidden=!this.hidden;this.applyFilter();}
    up(){this.go(parent(this.directory));}
    working(){this.go(this.workingDirectory);}
    select(index,page){this.selected=this.entries.length?Math.max(0,Math.min(this.entries.length-1,index)):-1;if(this.selected<this.top)this.top=Math.max(0,this.selected);if(this.selected>=this.top+page)this.top=this.selected-page+1;}
    path(name){return absolute(name,this.directory);}
}
