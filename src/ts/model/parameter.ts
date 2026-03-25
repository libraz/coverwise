/// Parameter definition for combinatorial test generation.

import { UNASSIGNED } from './test-case.js';

export { UNASSIGNED };

/**
 * A test parameter with a name and a set of discrete values.
 *
 * Values can be marked as invalid for negative testing. Invalid values are
 * excluded from positive test generation and used to create separate negative
 * test cases (one invalid value per test case).
 */
export class Parameter {
  readonly name: string;
  readonly values: string[];

  private invalid_: boolean[];
  private aliases_: string[][];
  private equivalenceClasses_: string[];

  constructor(name: string, values: string[]);
  constructor(name: string, values: string[], invalid: boolean[]);
  constructor(name: string, values: string[], invalid?: boolean[]) {
    this.name = name;
    this.values = values;
    this.invalid_ = invalid ?? [];
    this.aliases_ = [];
    this.equivalenceClasses_ = [];
  }

  /** Returns the number of values for this parameter. */
  get size(): number {
    return this.values.length;
  }

  /** Returns the number of valid values. */
  get validCount(): number {
    if (this.invalid_.length === 0) {
      return this.size;
    }
    let count = 0;
    for (let i = 0; i < this.size; ++i) {
      if (!this.isInvalid(i)) {
        ++count;
      }
    }
    return count;
  }

  /** Returns the number of invalid values. */
  get invalidCount(): number {
    return this.size - this.validCount;
  }

  /** Returns true if the value at the given index is marked invalid. */
  isInvalid(index: number): boolean {
    if (this.invalid_.length === 0) {
      return false;
    }
    if (index >= this.invalid_.length) {
      return false;
    }
    return this.invalid_[index];
  }

  /** Returns true if this parameter has any invalid values. */
  get hasInvalidValues(): boolean {
    return this.invalid_.some((v) => v);
  }

  /** Access the invalid flags array. */
  get invalid(): boolean[] {
    return this.invalid_;
  }

  /** Set the invalid flags array. */
  setInvalid(inv: boolean[]): void {
    this.invalid_ = inv;
  }

  /** Returns the aliases for the value at the given index. */
  aliases(index: number): string[] {
    if (this.aliases_.length === 0 || index >= this.aliases_.length) {
      return [];
    }
    return this.aliases_[index];
  }

  /** Returns true if any value has aliases. */
  get hasAliases(): boolean {
    return this.aliases_.some((a) => a.length > 0);
  }

  /**
   * Get a display name for a value, rotating through primary + aliases.
   *
   * For a value with aliases ["chrome", "edge"], rotation 0 returns the primary
   * value, rotation 1 returns "chrome", rotation 2 returns "edge", then wraps.
   */
  displayName(valueIndex: number, rotation: number): string {
    const aliasList = this.aliases(valueIndex);
    if (aliasList.length === 0) {
      return this.values[valueIndex];
    }
    const total = 1 + aliasList.length;
    const pick = rotation % total;
    if (pick === 0) {
      return this.values[valueIndex];
    }
    return aliasList[pick - 1];
  }

  /** Set the aliases for all values. */
  setAliases(aliases: string[][]): void {
    this.aliases_ = aliases;
  }

  /** Access the aliases array. */
  get allAliases(): string[][] {
    return this.aliases_;
  }

  /**
   * Find a value index by name, checking both primary values and aliases.
   * @returns The value index, or UNASSIGNED if not found.
   */
  findValueIndex(name: string, caseSensitive = true): number {
    const eq = caseSensitive
      ? (a: string, b: string) => a === b
      : (a: string, b: string) => a.toLowerCase() === b.toLowerCase();

    // Check primary values first.
    for (let i = 0; i < this.values.length; ++i) {
      if (eq(this.values[i], name)) {
        return i;
      }
    }
    // Check aliases.
    for (let i = 0; i < this.aliases_.length; ++i) {
      for (const alias of this.aliases_[i]) {
        if (eq(alias, name)) {
          return i;
        }
      }
    }
    return UNASSIGNED;
  }

  /** Returns the equivalence class for the value at the given index. */
  equivalenceClass(index: number): string {
    if (this.equivalenceClasses_.length === 0 || index >= this.equivalenceClasses_.length) {
      return '';
    }
    return this.equivalenceClasses_[index];
  }

  /** Returns true if any value has an equivalence class defined. */
  get hasEquivalenceClasses(): boolean {
    return this.equivalenceClasses_.some((c) => c.length > 0);
  }

  /** Returns the distinct class names (in first-seen order). */
  uniqueClasses(): string[] {
    const result: string[] = [];
    for (const c of this.equivalenceClasses_) {
      if (c.length > 0 && !result.includes(c)) {
        result.push(c);
      }
    }
    return result;
  }

  /** Set the equivalence class for each value. */
  setEquivalenceClasses(classes: string[]): void {
    this.equivalenceClasses_ = classes;
  }

  /** Access the equivalence classes array. */
  get equivalenceClasses(): string[] {
    return this.equivalenceClasses_;
  }
}

/** Check if any parameter in the collection has invalid values. */
export function hasInvalidValues(params: Parameter[]): boolean {
  return params.some((p) => p.hasInvalidValues);
}
