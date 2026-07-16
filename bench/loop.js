const start = performance.now();
let sum = 0;
for (let i = 1; i <= 10_000_000; i++) sum += i;
const elapsed = (performance.now() - start) / 1000;
console.log(`Sum: ${sum}`);
console.log(`Time: ${elapsed.toFixed(4)}s`);
