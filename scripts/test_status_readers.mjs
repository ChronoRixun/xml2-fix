// online-status.json compatibility (docs/online-party.md section 9): runs the ACTUAL `readStatus` of every released
// launcher (taken from git, read-only) over the lines this DLL writes - the v1 line, the v1.6 line with the party and
// process block, an attempt-error line and a v2 probe - and checks the released readers keep every v1 field.
//   node scripts/test_status_readers.mjs <ultimate-legends checkout> <samples.jsonl from XML2_STATUS_SAMPLES>
import { execFileSync } from 'node:child_process';
import { readFileSync } from 'node:fs';

const [repo, samplesPath] = process.argv.slice(2);
if (!repo || !samplesPath) { console.error('usage: test_status_readers.mjs <ultimate-legends> <samples.jsonl>'); process.exit(2); }
const refs = ['v0.4.0-rc.1', 'v0.4.0', 'v0.5.0-beta.1', 'v0.5.0-beta.2', 'origin/main'];
const file = 'src/launcher-ui/assets/js/app/online-view.js';
const samples = readFileSync(samplesPath, 'utf8').split('\n').filter(Boolean);
let failed = 0, passed = 0;
const check = (ok, what) => { console.log(`${ok ? 'PASS' : 'FAIL'} ${what}`); ok ? passed++ : failed++; };

// One top-level declaration's full text, by brace/bracket matching from its start.
function extract(source, start) {
    const at = source.indexOf(start);
    if (at < 0) throw new Error(`missing ${start}`);
    let i = source.indexOf(start.endsWith('[') ? '[' : start.startsWith('const') && !start.includes('(') ? '=' : '{', at);
    if (source[i] === '=') { const end = source.indexOf(';', i); return source.slice(at, end + 1); }
    const open = source[i], close = open === '[' ? ']' : '}';
    let depth = 0;
    for (; i < source.length; ++i) {
        if (source[i] === open) depth++;
        else if (source[i] === close && --depth === 0) break;
    }
    let end = i + 1;
    if (source[end] === ';') end++;
    return source.slice(at, end);
}

for (const ref of refs) {
    let source;
    try { source = execFileSync('git', ['-C', repo, 'show', `${ref}:${file}`], { encoding: 'utf8' }); }
    catch { check(false, `${ref}: online-view.js readable`); continue; }
    const parts = [
        extract(source, 'const ALPHABET'), extract(source, 'const CODE_LENGTH'), extract(source, 'const STATES'),
        extract(source, 'function normaliseCode('), extract(source, 'function checkCode('), extract(source, 'function readStatus(')
    ];
    const readStatus = new Function(`${parts.join('\n')}\nreturn readStatus;`)();
    // The backend hands the page the file parsed as one JSON object (rapidjson; JSON.parse is the same strict JSON).
    const read = samples.map(line => readStatus(JSON.parse(line)));
    const [legacy, party, attemptError, v2] = read;
    check(legacy && legacy.state === 'room' && legacy.code === 'K7QN2X9M' && legacy.players === 2, `${ref}: reads the v1 line`);
    check(!!party && JSON.stringify(party) === JSON.stringify(legacy), `${ref}: reads the v1.6 line (party + process block) exactly like the v1 line`);
    check(!!attemptError && JSON.stringify(attemptError) === JSON.stringify(legacy), `${ref}: an attempt error inside the party block does not change the v1 view`);
    check(!!v2 && v2.state === 'room', `${ref}: (probe) v:2 would also pass this reader - not needed, v stays 1`);
}
console.log(`${passed} PASS / ${failed} FAIL`);
process.exit(failed ? 1 : 0);
