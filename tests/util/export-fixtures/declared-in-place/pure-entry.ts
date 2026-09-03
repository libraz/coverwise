export interface Handle {
  close(): Promise<void>;
  readonly disposed: boolean;
}

export type { Report } from './vocabulary.js';
