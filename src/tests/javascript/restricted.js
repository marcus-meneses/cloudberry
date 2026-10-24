let denied=false;
try { system.nowNs(); } catch(e) { denied=e.name==='HandleError' && e.status===1 && e.frame instanceof Uint8Array; }
if(!Number.isNaN(Date.now()) || !Number.isNaN(new Date().getTime()))throw Error('unauthorized UTC fallback');
if(!denied)throw Error('missing manifest authorization');
console.log('PASS JS manifest denial');
