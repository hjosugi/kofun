'use strict';

const { spawn } = require('child_process');

class Client {
  constructor(server) {
    this.child = spawn(server, [], { stdio: ['pipe', 'pipe', 'inherit'] });
    this.buffer = Buffer.alloc(0);
    this.messages = [];
    this.waiters = [];
    this.child.stdout.on('data', (chunk) => {
      this.buffer = Buffer.concat([this.buffer, chunk]);
      this.drain();
    });
  }

  drain() {
    for (;;) {
      const headerEnd = this.buffer.indexOf('\r\n\r\n');
      if (headerEnd < 0) return;
      const header = this.buffer.subarray(0, headerEnd).toString('ascii');
      const match = /Content-Length:\s*(\d+)/i.exec(header);
      if (!match) throw new Error('server response omitted Content-Length');
      const length = Number(match[1]);
      if (this.buffer.length < headerEnd + 4 + length) return;
      const body = this.buffer.subarray(headerEnd + 4, headerEnd + 4 + length);
      this.buffer = this.buffer.subarray(headerEnd + 4 + length);
      const message = JSON.parse(body.toString('utf8'));
      this.messages.push(message);
      const pending = this.waiters;
      this.waiters = [];
      for (const waiter of pending) {
        if (waiter.predicate(message)) {
          clearTimeout(waiter.timer);
          waiter.resolve(message);
        } else {
          this.waiters.push(waiter);
        }
      }
    }
  }

  send(message, bytewise = false) {
    const body = Buffer.from(JSON.stringify(message), 'utf8');
    const frame = Buffer.concat([
      Buffer.from(`Content-Length: ${body.length}\r\nContent-Type: application/vscode-jsonrpc; charset=utf-8\r\n\r\n`),
      body
    ]);
    if (bytewise) {
      for (let i = 0; i < frame.length; i += 1) this.child.stdin.write(frame.subarray(i, i + 1));
    } else {
      this.child.stdin.write(frame);
    }
  }

  sendRaw(body) {
    const bytes = Buffer.from(body, 'utf8');
    this.child.stdin.write(`Content-Length: ${bytes.length}\r\n\r\n`);
    this.child.stdin.write(bytes);
  }

  waitFor(predicate, timeout = 5000) {
    const existing = this.messages.find(predicate);
    if (existing) return Promise.resolve(existing);
    return new Promise((resolve, reject) => {
      const waiter = { predicate, resolve, reject };
      waiter.timer = setTimeout(() => {
        this.waiters = this.waiters.filter((item) => item !== waiter);
        reject(new Error('timed out waiting for language server response'));
      }, timeout);
      this.waiters.push(waiter);
    });
  }

  async stop(nextId) {
    this.send({ jsonrpc: '2.0', id: nextId, method: 'shutdown', params: null });
    await this.waitFor((message) => message.id === nextId);
    this.send({ jsonrpc: '2.0', method: 'exit', params: null });
    this.child.stdin.end();
    await new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        this.child.kill();
        reject(new Error('language server did not exit'));
      }, 5000);
      this.child.once('exit', (code) => {
        clearTimeout(timer);
        if (code === 0) resolve();
        else reject(new Error(`language server exited with ${code}`));
      });
    });
  }
}

module.exports = { Client };
