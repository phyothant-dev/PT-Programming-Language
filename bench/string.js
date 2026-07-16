const start = performance.now();
let s = "";
for (let i = 0; i < 100_000; i++) s += "x";
const elapsed = (performance.now() - start) / 1000;
console.log(`Length: ${s.length}`);
console.log(`Time: ${elapsed.toFixed(4)}s`);
