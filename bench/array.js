const start = performance.now();
const arr = [];
for (let i = 0; i < 100_000; i++) arr.push(i * 2);
let sum = 0;
for (let i = 0; i < arr.length; i++) sum += arr[i];
const elapsed = (performance.now() - start) / 1000;
console.log(`Sum: ${sum}`);
console.log(`Time: ${elapsed.toFixed(4)}s`);
