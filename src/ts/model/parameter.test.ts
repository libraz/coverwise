import { describe, expect, it } from 'vitest';
import { hasInvalidValues, Parameter, UNASSIGNED } from './parameter.js';

describe('Parameter', () => {
  describe('constructor', () => {
    it('creates a parameter with name and values', () => {
      const p = new Parameter('os', ['win', 'mac', 'linux']);
      expect(p.name).toBe('os');
      expect(p.values).toEqual(['win', 'mac', 'linux']);
    });

    it('creates a parameter with invalid flags', () => {
      const p = new Parameter('os', ['win', 'mac', 'linux'], [false, true, false]);
      expect(p.name).toBe('os');
      expect(p.isInvalid(1)).toBe(true);
      expect(p.isInvalid(0)).toBe(false);
    });
  });

  describe('size', () => {
    it('returns the number of values', () => {
      const p = new Parameter('browser', ['chrome', 'firefox']);
      expect(p.size).toBe(2);
    });

    it('returns 0 for empty values', () => {
      const p = new Parameter('empty', []);
      expect(p.size).toBe(0);
    });
  });

  describe('validCount / invalidCount', () => {
    it('returns all values as valid when no invalid flags', () => {
      const p = new Parameter('os', ['win', 'mac', 'linux']);
      expect(p.validCount).toBe(3);
      expect(p.invalidCount).toBe(0);
    });

    it('counts valid and invalid values correctly', () => {
      const p = new Parameter('os', ['win', 'mac', 'linux'], [false, true, true]);
      expect(p.validCount).toBe(1);
      expect(p.invalidCount).toBe(2);
    });

    it('handles all invalid', () => {
      const p = new Parameter('x', ['a', 'b'], [true, true]);
      expect(p.validCount).toBe(0);
      expect(p.invalidCount).toBe(2);
    });
  });

  describe('isInvalid', () => {
    it('returns false when no invalid flags are set', () => {
      const p = new Parameter('os', ['win', 'mac']);
      expect(p.isInvalid(0)).toBe(false);
      expect(p.isInvalid(1)).toBe(false);
    });

    it('returns correct flag for in-range index', () => {
      const p = new Parameter('os', ['win', 'mac'], [true, false]);
      expect(p.isInvalid(0)).toBe(true);
      expect(p.isInvalid(1)).toBe(false);
    });

    it('returns false for out-of-range index', () => {
      const p = new Parameter('os', ['win', 'mac'], [true, false]);
      expect(p.isInvalid(5)).toBe(false);
    });
  });

  describe('hasInvalidValues', () => {
    it('returns true when some values are invalid', () => {
      const p = new Parameter('os', ['win', 'mac'], [false, true]);
      expect(p.hasInvalidValues).toBe(true);
    });

    it('returns false when no values are invalid', () => {
      const p = new Parameter('os', ['win', 'mac']);
      expect(p.hasInvalidValues).toBe(false);
    });

    it('returns false when all flags are false', () => {
      const p = new Parameter('os', ['win', 'mac'], [false, false]);
      expect(p.hasInvalidValues).toBe(false);
    });
  });

  describe('aliases', () => {
    it('returns empty array when no aliases set', () => {
      const p = new Parameter('browser', ['chrome', 'firefox']);
      expect(p.aliases(0)).toEqual([]);
    });

    it('returns aliases for the given index', () => {
      const p = new Parameter('browser', ['chrome', 'firefox']);
      p.setAliases([['chromium'], ['ff', 'mozilla']]);
      expect(p.aliases(0)).toEqual(['chromium']);
      expect(p.aliases(1)).toEqual(['ff', 'mozilla']);
    });

    it('returns empty array for out-of-range index', () => {
      const p = new Parameter('browser', ['chrome']);
      p.setAliases([['chromium']]);
      expect(p.aliases(5)).toEqual([]);
    });
  });

  describe('hasAliases', () => {
    it('returns false when no aliases are set', () => {
      const p = new Parameter('browser', ['chrome']);
      expect(p.hasAliases).toBe(false);
    });

    it('returns true when at least one value has aliases', () => {
      const p = new Parameter('browser', ['chrome', 'firefox']);
      p.setAliases([['chromium'], []]);
      expect(p.hasAliases).toBe(true);
    });

    it('returns false when alias arrays are all empty', () => {
      const p = new Parameter('browser', ['chrome']);
      p.setAliases([[]]);
      expect(p.hasAliases).toBe(false);
    });
  });

  describe('displayName', () => {
    it('returns primary value when no aliases', () => {
      const p = new Parameter('os', ['win', 'mac']);
      expect(p.displayName(0, 0)).toBe('win');
      expect(p.displayName(0, 1)).toBe('win');
      expect(p.displayName(1, 5)).toBe('mac');
    });

    it('rotates through primary and aliases', () => {
      const p = new Parameter('browser', ['chrome']);
      p.setAliases([['edge', 'brave']]);
      // total = 1 + 2 = 3; pick = rotation % 3
      expect(p.displayName(0, 0)).toBe('chrome'); // pick=0 -> primary
      expect(p.displayName(0, 1)).toBe('edge'); // pick=1 -> alias[0]
      expect(p.displayName(0, 2)).toBe('brave'); // pick=2 -> alias[1]
      expect(p.displayName(0, 3)).toBe('chrome'); // pick=0 -> wraps
    });
  });

  describe('findValueIndex', () => {
    it('finds primary value by name', () => {
      const p = new Parameter('os', ['win', 'mac', 'linux']);
      expect(p.findValueIndex('mac')).toBe(1);
    });

    it('finds value via alias', () => {
      const p = new Parameter('browser', ['chrome', 'firefox']);
      p.setAliases([['chromium'], ['ff']]);
      expect(p.findValueIndex('ff')).toBe(1);
      expect(p.findValueIndex('chromium')).toBe(0);
    });

    it('returns UNASSIGNED when not found', () => {
      const p = new Parameter('os', ['win', 'mac']);
      expect(p.findValueIndex('linux')).toBe(UNASSIGNED);
    });

    it('is case-sensitive by default', () => {
      const p = new Parameter('os', ['Win', 'Mac']);
      expect(p.findValueIndex('win')).toBe(UNASSIGNED);
      expect(p.findValueIndex('Win')).toBe(0);
    });

    it('supports case-insensitive search', () => {
      const p = new Parameter('os', ['Win', 'Mac']);
      expect(p.findValueIndex('win', false)).toBe(0);
      expect(p.findValueIndex('MAC', false)).toBe(1);
    });

    it('finds alias case-insensitively', () => {
      const p = new Parameter('browser', ['chrome']);
      p.setAliases([['Chromium']]);
      expect(p.findValueIndex('chromium', false)).toBe(0);
    });
  });

  describe('equivalenceClass', () => {
    it('returns empty string when no classes set', () => {
      const p = new Parameter('os', ['win', 'mac']);
      expect(p.equivalenceClass(0)).toBe('');
    });

    it('returns class name for given index', () => {
      const p = new Parameter('os', ['win', 'mac', 'linux']);
      p.setEquivalenceClasses(['desktop', 'desktop', 'server']);
      expect(p.equivalenceClass(0)).toBe('desktop');
      expect(p.equivalenceClass(2)).toBe('server');
    });

    it('returns empty string for out-of-range index', () => {
      const p = new Parameter('os', ['win']);
      p.setEquivalenceClasses(['desktop']);
      expect(p.equivalenceClass(5)).toBe('');
    });
  });

  describe('hasEquivalenceClasses', () => {
    it('returns false when no classes set', () => {
      const p = new Parameter('os', ['win']);
      expect(p.hasEquivalenceClasses).toBe(false);
    });

    it('returns true when at least one class is set', () => {
      const p = new Parameter('os', ['win', 'mac']);
      p.setEquivalenceClasses(['desktop', '']);
      expect(p.hasEquivalenceClasses).toBe(true);
    });

    it('returns false when all classes are empty strings', () => {
      const p = new Parameter('os', ['win', 'mac']);
      p.setEquivalenceClasses(['', '']);
      expect(p.hasEquivalenceClasses).toBe(false);
    });
  });

  describe('uniqueClasses', () => {
    it('returns empty array when no classes set', () => {
      const p = new Parameter('os', ['win']);
      expect(p.uniqueClasses()).toEqual([]);
    });

    it('returns distinct class names in first-seen order', () => {
      const p = new Parameter('os', ['win', 'mac', 'linux', 'bsd']);
      p.setEquivalenceClasses(['desktop', 'desktop', 'server', 'server']);
      expect(p.uniqueClasses()).toEqual(['desktop', 'server']);
    });

    it('skips empty class names', () => {
      const p = new Parameter('os', ['win', 'mac']);
      p.setEquivalenceClasses(['', 'desktop']);
      expect(p.uniqueClasses()).toEqual(['desktop']);
    });
  });
});

describe('hasInvalidValues (utility function)', () => {
  it('returns true if any parameter has invalid values', () => {
    const params = [
      new Parameter('os', ['win', 'mac']),
      new Parameter('browser', ['chrome', 'ie'], [false, true]),
    ];
    expect(hasInvalidValues(params)).toBe(true);
  });

  it('returns false if no parameter has invalid values', () => {
    const params = [
      new Parameter('os', ['win', 'mac']),
      new Parameter('browser', ['chrome', 'firefox']),
    ];
    expect(hasInvalidValues(params)).toBe(false);
  });

  it('returns false for empty array', () => {
    expect(hasInvalidValues([])).toBe(false);
  });
});
