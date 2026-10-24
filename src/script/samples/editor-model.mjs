/* Pure document model, shared by the sample and focused host tests. Columns
 * count Unicode scalars, not UTF-16 code units. No OS or graphics dependency. */
export function decodeUTF8(bytes) {
    let result = '';
    for (let i = 0; i < bytes.length;) {
        const first = bytes[i++];
        let cp = first, count = 0, minimum = 0;
        if (first >= 0xc2 && first <= 0xdf) { cp &= 31; count = 1; minimum = 0x80; }
        else if (first >= 0xe0 && first <= 0xef) { cp &= 15; count = 2; minimum = 0x800; }
        else if (first >= 0xf0 && first <= 0xf4) { cp &= 7; count = 3; minimum = 0x10000; }
        else if (first >= 0x80) throw Error('Not a valid UTF-8 text file');
        for (let n = 0; n < count; n++) {
            if (i === bytes.length || (bytes[i] & 0xc0) !== 0x80) throw Error('Not a valid UTF-8 text file');
            cp = (cp << 6) | (bytes[i++] & 63);
        }
        if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) throw Error('Not a valid UTF-8 text file');
        result += String.fromCodePoint(cp);
    }
    return result;
}
export class Document {
    constructor(text = '', path = '') { this.reset(text, path); this.overwrite = false; this.clipboard = ''; }
    reset(text, path = '') {
        this.eol = text.includes('\r\n') ? '\r\n' : '\n';
        this.bom = text.startsWith('\ufeff');
        if (this.bom) text = text.slice(1);
        this.lines = text.replace(/\r\n/g, '\n').split('\n');
        this.path = path; this.row = this.col = this.top = this.left = 0;
        this.undoStack = []; this.redoStack = []; this.saved = this.value();
    }
    value() { return (this.bom ? '\ufeff' : '') + this.lines.join(this.eol); }
    get dirty() { return this.value() !== this.saved; }
    savedAs(path) { this.path = path; this.saved = this.value(); }
    snapshot() { return {lines: this.lines.slice(), row: this.row, col: this.col}; }
    restore(snapshot) { this.lines = snapshot.lines.slice(); this.row = snapshot.row; this.col = snapshot.col; }
    change(action) { const before = this.snapshot(); action(); if (this.lines.join('\n') !== before.lines.join('\n')) { this.undoStack.push(before); this.redoStack = []; } }
    length(row = this.row) { return Array.from(this.lines[row]).length; }
    insert(text) {
        text = text.replace(/\r\n/g, '\n');
        this.change(() => {
            const chars = Array.from(this.lines[this.row]);
            const before = chars.slice(0, this.col).join('');
            const tail = this.col + (this.overwrite && !text.includes('\n') ? Array.from(text).length : 0);
            const after = chars.slice(tail).join(''), parts = (before + text + after).split('\n');
            this.lines.splice(this.row, 1, ...parts);
            const added = text.split('\n'); this.row += added.length - 1;
            this.col = added.length === 1 ? this.col + Array.from(text).length : Array.from(added.at(-1)).length;
        });
    }
    backspace() { this.change(() => { if (this.col) { const c = Array.from(this.lines[this.row]); c.splice(--this.col, 1); this.lines[this.row] = c.join(''); } else if (this.row) { this.col = this.length(this.row - 1); this.lines[this.row - 1] += this.lines[this.row]; this.lines.splice(this.row--, 1); } }); }
    delete() { this.change(() => { if (this.col < this.length()) { const c = Array.from(this.lines[this.row]); c.splice(this.col, 1); this.lines[this.row] = c.join(''); } else if (this.row + 1 < this.lines.length) { this.lines[this.row] += this.lines[this.row + 1]; this.lines.splice(this.row + 1, 1); } }); }
    scroll(rows) { this.top=Math.max(0,Math.min(this.lines.length-1,this.top+rows)); }
    move(key, page = 10) {
        if (key === 'left') { if (this.col) this.col--; else if (this.row) { this.row--; this.col = this.length(); } }
        else if (key === 'right') { if (this.col < this.length()) this.col++; else if (this.row + 1 < this.lines.length) { this.row++; this.col = 0; } }
        else if (key === 'home') this.col = 0;
        else if (key === 'end') this.col = this.length();
        else { const delta = key === 'up' ? -1 : key === 'down' ? 1 : key === 'pageup' ? -page : page; this.row = Math.max(0, Math.min(this.lines.length - 1, this.row + delta)); this.col = Math.min(this.col, this.length()); }
    }
    undo() { if (this.undoStack.length) { this.redoStack.push(this.snapshot()); this.restore(this.undoStack.pop()); } }
    redo() { if (this.redoStack.length) { this.undoStack.push(this.snapshot()); this.restore(this.redoStack.pop()); } }
    copy() { this.clipboard = this.lines[this.row] + '\n'; }
    cut() { this.copy(); this.change(() => { if (this.lines.length === 1) this.lines[0] = ''; else this.lines.splice(this.row, 1); this.row = Math.min(this.row, this.lines.length - 1); this.col = 0; }); }
    find(needle) {
        if (!needle) return false;
        for (let n = 0; n <= this.lines.length; n++) {
            const row = (this.row + n) % this.lines.length;
            const start = n === 0 ? Array.from(this.lines[row]).slice(0, this.col + 1).join('').length : 0;
            const at = this.lines[row].indexOf(needle, start);
            if (at >= 0) { this.row = row; this.col = Array.from(this.lines[row].slice(0, at)).length; return true; }
        }
        return false;
    }
}
