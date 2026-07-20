'use strict';

const a = 10000000000000000;
const b = -10000000000000000;
const c = 1;
const left = (a + b) + c;
const right = a + (b + c);

process.stdout.write(`left=${left}\n`);
process.stdout.write(`right=${right}\n`);
process.stdout.write(`equal=${left === right}\n`);
process.stdout.write(`counterexample=[${a},${b},${c}]\n`);
