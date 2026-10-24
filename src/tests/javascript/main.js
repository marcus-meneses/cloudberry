import { system as os } from 'cloudberry';
import { message, nested } from './companion.js';
import { message as cached } from '././companion.js';
function check(value, message) { if (!value) throw Error(message); }
check(os === system && os.args.length === 3 && os.args[1] === 'one' && os.args[2] === 'two words', 'argv');
check(os.cwd === '/' && moduleLoads === 1 && cached === message && nested.endsWith('/companion.js'), 'cwd/module cache/meta');
check(message === 'ação 🍓' && Math.sqrt(9) === 3 && typeof os.nowNs() === 'bigint', 'engine/timestamp');
let start = os.nowNs();
await Promise.all([os.sleep(3), os.sleep(1), os.sleep(2)]);
check(os.nowNs() >= start + 3000000n, 'deadline');
try { os.randomBytes(77); } catch (e) { check(e.name === 'HandleError' && e.status === 2, 'entropy error'); }
let data = new Uint8Array(12001); for (let i=0;i<data.length;i++) data[i]=(i*37+11)&255;
os.writeFile('/javascript.bin',data);os.writeFile('/javascript.bin','tail',true);
let loaded=os.readFile('/javascript.bin');check(loaded.length === data.length+4 && loaded[data.length]===116,`file length/append ${loaded.length}/${data.length}/${loaded[data.length]}`);
for(let i=0;i<data.length;i++)check(loaded[i]===data[i],'file bytes');
let file=os.open('/javascript.bin');let total=0;for(;;){let bytes=await file.read(700);if(!bytes.length)break;total+=bytes.length;}file.close();check(total===12005,'file stream');
let pipe=os.pipe(), output=[];
let reader=(async()=>{for(;;){let bytes=await pipe.reader.read();if(!bytes.length)break;output.push(...bytes);}pipe.reader.close();})();
let writer=(async()=>{check(await pipe.writer.write(new Uint8Array(40001).fill(77))===40001n,'write count');pipe.writer.close();})();
await Promise.all([reader,writer]);check(output.length===40001 && output.every(x=>x===77),'partial pipe/backpressure/EOF');
pipe=os.pipe();let blocked=pipe.reader.read();let busy=false;try{pipe.reader.close();}catch(e){busy=e.status===11;}check(busy,'busy close');pipe.writer.close();check((await blocked).length===0,'pending EOF');pipe.reader.close();
pipe=os.pipe();let direction=false;try{await pipe.reader.write('bad');}catch(e){direction=e.name==='HandleError' && typeof e.status==='number';}check(direction,'direction error');pipe.reader.close();pipe.writer.close();
let missing=false;try{os.readFile('/no-such-js-file');}catch(e){missing=e.name==='HandleError' && typeof e.status==='number' && e.frame instanceof Uint8Array;}check(missing,'structured file error');
let foreign=false;try{await os.wait(18446744073709551615n);}catch(e){foreign=e.name==='HandleError' && e.status===1 && e.process===18446744073709551615n && typeof e.requestId==='bigint' && e.frame.length>0;}check(foreign,'lossless owner denial');
let process=os.exec('true');check(typeof process.process==='bigint','process ID');let result=await process;check(result.exitCode===0n && result.state===2,'async child');
pipe=os.pipe();let child=os.exec('echo',['child-output'],{stdout:pipe.writer,cwd:'/system'});pipe.writer.close();let bytes=await pipe.reader.read();check(String.fromCharCode(...bytes)==='child-output\n','redirected child stdout');check((await child).exitCode===0n,'child completion');check((await pipe.reader.read()).length===0,'child EOF');pipe.reader.close();
console.log('PASS JS bindings: files streams backpressure timers processes errors modules argv cwd');
