import path from 'node:path';
import { defineConfig } from 'vitest/config';

export default defineConfig({
  resolve: {
    alias: {
      '../coverwise.js': path.resolve(__dirname, 'dist/coverwise.js'),
    },
  },
  test: {
    globals: true,
    environment: 'node',
    include: ['js/**/*.test.ts', 'tests/wasm/**/*.test.ts', 'src/ts/**/*.test.ts'],

    coverage: {
      provider: 'v8',
      reporter: ['text', 'json', 'json-summary', 'html'],
      include: ['js/**/*.ts', 'src/ts/**/*.ts'],
      exclude: ['**/*.test.ts', '**/*.d.ts'],
      thresholds: {
        statements: 85,
        branches: 80,
        functions: 90,
        lines: 85,
        'js/**': {
          statements: 80,
          branches: 75,
          functions: 65,
          lines: 80,
        },
        'src/ts/**': {
          statements: 85,
          branches: 80,
          functions: 90,
          lines: 85,
        },
      },
    },
  },
});
