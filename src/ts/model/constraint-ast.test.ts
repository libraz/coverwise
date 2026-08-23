import { describe, expect, it } from 'vitest';
import {
  AndNode,
  ConstraintResult,
  EqualsNode,
  globMatch,
  IfThenElseNode,
  ImpliesNode,
  InNode,
  LikeNode,
  NotEqualsNode,
  NotNode,
  OrNode,
  ParamEqualsNode,
  ParamNotEqualsNode,
  RelationalNode,
  RelOp,
  UNASSIGNED,
} from './constraint-ast.js';

const { True, False, Unknown } = ConstraintResult;

describe('EqualsNode', () => {
  it('returns True when assigned value matches', () => {
    const node = new EqualsNode(0, 1);
    expect(node.evaluate([1])).toBe(True);
  });

  it('returns False when assigned value does not match', () => {
    const node = new EqualsNode(0, 1);
    expect(node.evaluate([0])).toBe(False);
  });

  it('returns Unknown when parameter is unassigned', () => {
    const node = new EqualsNode(0, 1);
    expect(node.evaluate([UNASSIGNED])).toBe(Unknown);
  });

  it('returns Unknown when param index is out of range', () => {
    const node = new EqualsNode(5, 1);
    expect(node.evaluate([0])).toBe(Unknown);
  });
});

describe('NotEqualsNode', () => {
  it('returns True when assigned value differs', () => {
    const node = new NotEqualsNode(0, 1);
    expect(node.evaluate([0])).toBe(True);
  });

  it('returns False when assigned value matches', () => {
    const node = new NotEqualsNode(0, 1);
    expect(node.evaluate([1])).toBe(False);
  });

  it('returns Unknown when parameter is unassigned', () => {
    const node = new NotEqualsNode(0, 1);
    expect(node.evaluate([UNASSIGNED])).toBe(Unknown);
  });

  it('returns Unknown when param index is out of range', () => {
    const node = new NotEqualsNode(5, 0);
    expect(node.evaluate([0])).toBe(Unknown);
  });
});

describe('AndNode', () => {
  it('returns True when both are True', () => {
    const node = new AndNode(new EqualsNode(0, 0), new EqualsNode(1, 1));
    expect(node.evaluate([0, 1])).toBe(True);
  });

  it('returns False when left is False', () => {
    const node = new AndNode(new EqualsNode(0, 0), new EqualsNode(1, 1));
    expect(node.evaluate([1, 1])).toBe(False);
  });

  it('returns False when right is False', () => {
    const node = new AndNode(new EqualsNode(0, 0), new EqualsNode(1, 1));
    expect(node.evaluate([0, 0])).toBe(False);
  });

  it('returns Unknown when left is True and right is Unknown', () => {
    const node = new AndNode(new EqualsNode(0, 0), new EqualsNode(1, 1));
    expect(node.evaluate([0, UNASSIGNED])).toBe(Unknown);
  });

  it('returns False when left is False and right is Unknown (short-circuit)', () => {
    const node = new AndNode(new EqualsNode(0, 0), new EqualsNode(1, 1));
    expect(node.evaluate([1, UNASSIGNED])).toBe(False);
  });
});

describe('OrNode', () => {
  it('returns True when both are True', () => {
    const node = new OrNode(new EqualsNode(0, 0), new EqualsNode(1, 1));
    expect(node.evaluate([0, 1])).toBe(True);
  });

  it('returns True when left is True and right is False', () => {
    const node = new OrNode(new EqualsNode(0, 0), new EqualsNode(1, 1));
    expect(node.evaluate([0, 0])).toBe(True);
  });

  it('returns False when both are False', () => {
    const node = new OrNode(new EqualsNode(0, 0), new EqualsNode(1, 1));
    expect(node.evaluate([1, 0])).toBe(False);
  });

  it('returns Unknown when left is False and right is Unknown', () => {
    const node = new OrNode(new EqualsNode(0, 0), new EqualsNode(1, 1));
    expect(node.evaluate([1, UNASSIGNED])).toBe(Unknown);
  });

  it('returns True when left is True and right is Unknown (short-circuit)', () => {
    const node = new OrNode(new EqualsNode(0, 0), new EqualsNode(1, 1));
    expect(node.evaluate([0, UNASSIGNED])).toBe(True);
  });
});

describe('NotNode', () => {
  it('returns False when child is True', () => {
    const node = new NotNode(new EqualsNode(0, 0));
    expect(node.evaluate([0])).toBe(False);
  });

  it('returns True when child is False', () => {
    const node = new NotNode(new EqualsNode(0, 0));
    expect(node.evaluate([1])).toBe(True);
  });

  it('returns Unknown when child is Unknown', () => {
    const node = new NotNode(new EqualsNode(0, 0));
    expect(node.evaluate([UNASSIGNED])).toBe(Unknown);
  });
});

describe('ImpliesNode', () => {
  it('returns True when antecedent is False', () => {
    const node = new ImpliesNode(new EqualsNode(0, 0), new EqualsNode(1, 1));
    expect(node.evaluate([1, 0])).toBe(True);
  });

  it('returns True when antecedent is True and consequent is True', () => {
    const node = new ImpliesNode(new EqualsNode(0, 0), new EqualsNode(1, 1));
    expect(node.evaluate([0, 1])).toBe(True);
  });

  it('returns False when antecedent is True and consequent is False', () => {
    const node = new ImpliesNode(new EqualsNode(0, 0), new EqualsNode(1, 1));
    expect(node.evaluate([0, 0])).toBe(False);
  });

  it('returns Unknown when antecedent is True and consequent is Unknown', () => {
    const node = new ImpliesNode(new EqualsNode(0, 0), new EqualsNode(1, 1));
    expect(node.evaluate([0, UNASSIGNED])).toBe(Unknown);
  });

  it('returns True when antecedent is Unknown and consequent is True', () => {
    const node = new ImpliesNode(new EqualsNode(0, 0), new EqualsNode(1, 1));
    expect(node.evaluate([UNASSIGNED, 1])).toBe(True);
  });

  it('returns Unknown when antecedent is Unknown and consequent is False', () => {
    const node = new ImpliesNode(new EqualsNode(0, 0), new EqualsNode(1, 1));
    expect(node.evaluate([UNASSIGNED, 0])).toBe(Unknown);
  });
});

describe('IfThenElseNode', () => {
  it('evaluates then-branch when condition is True', () => {
    const node = new IfThenElseNode(
      new EqualsNode(0, 0),
      new EqualsNode(1, 1),
      new EqualsNode(1, 0),
    );
    // condition true, then-branch: param1==1
    expect(node.evaluate([0, 1])).toBe(True);
    expect(node.evaluate([0, 0])).toBe(False);
  });

  it('evaluates else-branch when condition is False', () => {
    const node = new IfThenElseNode(
      new EqualsNode(0, 0),
      new EqualsNode(1, 1),
      new EqualsNode(1, 0),
    );
    // condition false, else-branch: param1==0
    expect(node.evaluate([1, 0])).toBe(True);
    expect(node.evaluate([1, 1])).toBe(False);
  });

  it('returns agreed result when condition is Unknown and branches agree', () => {
    const node = new IfThenElseNode(
      new EqualsNode(0, 0),
      new EqualsNode(1, 1),
      new EqualsNode(1, 1),
    );
    // condition unknown, both branches evaluate to True
    expect(node.evaluate([UNASSIGNED, 1])).toBe(True);
  });

  it('returns Unknown when condition is Unknown and branches disagree', () => {
    const node = new IfThenElseNode(
      new EqualsNode(0, 0),
      new EqualsNode(1, 1),
      new EqualsNode(1, 0),
    );
    // condition unknown, then=True, else=False
    expect(node.evaluate([UNASSIGNED, 1])).toBe(Unknown);
  });
});

describe('RelationalNode', () => {
  describe('fromLiteral', () => {
    it('evaluates greater-than correctly', () => {
      const node = RelationalNode.fromLiteral(0, RelOp.Greater, 5, ['3', '7', '10']);
      expect(node.evaluate([0])).toBe(False); // 3 > 5 = false
      expect(node.evaluate([1])).toBe(True); // 7 > 5 = true
    });

    it('evaluates greater-or-equal correctly', () => {
      const node = RelationalNode.fromLiteral(0, RelOp.GreaterEqual, 5, ['3', '5', '7']);
      expect(node.evaluate([0])).toBe(False); // 3 >= 5 = false
      expect(node.evaluate([1])).toBe(True); // 5 >= 5 = true
      expect(node.evaluate([2])).toBe(True); // 7 >= 5 = true
    });

    it('evaluates less-than correctly', () => {
      const node = RelationalNode.fromLiteral(0, RelOp.Less, 5, ['3', '7']);
      expect(node.evaluate([0])).toBe(True); // 3 < 5 = true
      expect(node.evaluate([1])).toBe(False); // 7 < 5 = false
    });

    it('evaluates less-or-equal correctly', () => {
      const node = RelationalNode.fromLiteral(0, RelOp.LessEqual, 5, ['3', '5', '7']);
      expect(node.evaluate([0])).toBe(True); // 3 <= 5 = true
      expect(node.evaluate([1])).toBe(True); // 5 <= 5 = true
      expect(node.evaluate([2])).toBe(False); // 7 <= 5 = false
    });

    it('returns False for non-numeric value', () => {
      const node = RelationalNode.fromLiteral(0, RelOp.Greater, 5, ['abc', '10']);
      expect(node.evaluate([0])).toBe(False);
      expect(node.evaluate([1])).toBe(True);
    });

    it('returns Unknown for unassigned parameter', () => {
      const node = RelationalNode.fromLiteral(0, RelOp.Greater, 5, ['10']);
      expect(node.evaluate([UNASSIGNED])).toBe(Unknown);
    });
  });

  describe('fromParams', () => {
    it('compares two parameter values', () => {
      const node = RelationalNode.fromParams(0, RelOp.Less, 1, ['3', '7'], ['5', '2']);
      expect(node.evaluate([0, 0])).toBe(True); // 3 < 5 = true
      expect(node.evaluate([1, 1])).toBe(False); // 7 < 2 = false
    });

    it('returns Unknown when right param is unassigned', () => {
      const node = RelationalNode.fromParams(0, RelOp.Less, 1, ['3'], ['5']);
      expect(node.evaluate([0, UNASSIGNED])).toBe(Unknown);
    });

    it('returns False when right param value is non-numeric', () => {
      const node = RelationalNode.fromParams(0, RelOp.Less, 1, ['3'], ['abc']);
      expect(node.evaluate([0, 0])).toBe(False);
    });
  });
});

describe('InNode', () => {
  it('returns True when value is in set', () => {
    const node = new InNode(0, [1, 3, 5]);
    expect(node.evaluate([3])).toBe(True);
  });

  it('returns False when value is not in set', () => {
    const node = new InNode(0, [1, 3, 5]);
    expect(node.evaluate([2])).toBe(False);
  });

  it('returns Unknown when parameter is unassigned', () => {
    const node = new InNode(0, [1, 3]);
    expect(node.evaluate([UNASSIGNED])).toBe(Unknown);
  });

  it('returns Unknown when param index is out of range', () => {
    const node = new InNode(5, [0]);
    expect(node.evaluate([0])).toBe(Unknown);
  });
});

describe('LikeNode', () => {
  it('matches with * wildcard', () => {
    const node = new LikeNode(0, 'win*', ['win10', 'win11', 'mac']);
    expect(node.evaluate([0])).toBe(True);
    expect(node.evaluate([1])).toBe(True);
    expect(node.evaluate([2])).toBe(False);
  });

  it('matches with ? wildcard', () => {
    const node = new LikeNode(0, 'v?', ['v1', 'v2', 'v10']);
    expect(node.evaluate([0])).toBe(True);
    expect(node.evaluate([1])).toBe(True);
    expect(node.evaluate([2])).toBe(False);
  });

  it('matches exact string', () => {
    const node = new LikeNode(0, 'win', ['win', 'mac']);
    expect(node.evaluate([0])).toBe(True);
    expect(node.evaluate([1])).toBe(False);
  });

  it('returns Unknown for unassigned parameter', () => {
    const node = new LikeNode(0, '*', ['anything']);
    expect(node.evaluate([UNASSIGNED])).toBe(Unknown);
  });

  it('returns False for out-of-range value index', () => {
    const node = new LikeNode(0, '*', ['a']);
    expect(node.evaluate([5])).toBe(False);
  });

  it('matches case-insensitively by default and exactly when caseSensitive', () => {
    const insensitive = new LikeNode(0, 'chrome*', ['Chrome', 'Firefox']);
    expect(insensitive.evaluate([0])).toBe(True);
    expect(insensitive.evaluate([1])).toBe(False);

    const sensitive = new LikeNode(0, 'chrome*', ['Chrome', 'Firefox'], true);
    expect(sensitive.evaluate([0])).toBe(False);
    expect(new LikeNode(0, 'Chrome*', ['Chrome', 'Firefox'], true).evaluate([0])).toBe(True);
  });
});

describe('ParamEqualsNode', () => {
  it('returns True when string values match', () => {
    const node = new ParamEqualsNode(0, 1, ['a', 'b'], ['b', 'a']);
    expect(node.evaluate([0, 1])).toBe(True); // leftValues[0]='a', rightValues[1]='a'
  });

  it('returns False when string values differ', () => {
    const node = new ParamEqualsNode(0, 1, ['a', 'b'], ['c', 'd']);
    expect(node.evaluate([0, 0])).toBe(False); // 'a' != 'c'
  });

  it('returns Unknown when a parameter is unassigned', () => {
    const node = new ParamEqualsNode(0, 1, ['a'], ['a']);
    expect(node.evaluate([0, UNASSIGNED])).toBe(Unknown);
    expect(node.evaluate([UNASSIGNED, 0])).toBe(Unknown);
  });

  it('returns Unknown when param index is out of range', () => {
    const node = new ParamEqualsNode(0, 5, ['a'], ['a']);
    expect(node.evaluate([0])).toBe(Unknown);
  });

  it('returns False when value index is out of range', () => {
    const node = new ParamEqualsNode(0, 1, ['a'], ['b']);
    expect(node.evaluate([5, 0])).toBe(False);
  });
});

describe('ParamNotEqualsNode', () => {
  it('returns True when string values differ', () => {
    const node = new ParamNotEqualsNode(0, 1, ['a', 'b'], ['c', 'd']);
    expect(node.evaluate([0, 0])).toBe(True); // 'a' != 'c'
  });

  it('returns False when string values match', () => {
    const node = new ParamNotEqualsNode(0, 1, ['a', 'b'], ['b', 'a']);
    expect(node.evaluate([0, 1])).toBe(False); // 'a' == 'a'
  });

  it('returns Unknown when a parameter is unassigned', () => {
    const node = new ParamNotEqualsNode(0, 1, ['a'], ['a']);
    expect(node.evaluate([UNASSIGNED, 0])).toBe(Unknown);
  });

  it('returns False when value index is out of range', () => {
    const node = new ParamNotEqualsNode(0, 1, ['a'], ['b']);
    expect(node.evaluate([5, 0])).toBe(False);
  });
});

describe('globMatch', () => {
  it('matches exact strings', () => {
    expect(globMatch('hello', 'hello')).toBe(true);
    expect(globMatch('hello', 'world')).toBe(false);
  });

  it('matches * wildcard for any sequence', () => {
    expect(globMatch('he*', 'hello')).toBe(true);
    expect(globMatch('*lo', 'hello')).toBe(true);
    expect(globMatch('h*o', 'hello')).toBe(true);
    expect(globMatch('*', 'anything')).toBe(true);
    expect(globMatch('*', '')).toBe(true);
  });

  it('matches ? wildcard for single character', () => {
    expect(globMatch('h?llo', 'hello')).toBe(true);
    expect(globMatch('h?llo', 'hallo')).toBe(true);
    expect(globMatch('h?llo', 'hlo')).toBe(false);
    expect(globMatch('?', '界')).toBe(true);
    expect(globMatch('?', '😀')).toBe(true);
    expect(globMatch('?', '😀😀')).toBe(false);
  });

  it('handles combined wildcards', () => {
    expect(globMatch('h*l?o', 'hello')).toBe(true);
    expect(globMatch('?*', 'a')).toBe(true);
    expect(globMatch('?*', '')).toBe(false);
  });

  it('handles empty pattern and text', () => {
    expect(globMatch('', '')).toBe(true);
    expect(globMatch('', 'a')).toBe(false);
    expect(globMatch('*', '')).toBe(true);
  });

  it('handles multiple consecutive stars', () => {
    expect(globMatch('**', 'anything')).toBe(true);
    expect(globMatch('a**b', 'aXYZb')).toBe(true);
  });
});

describe('ConstraintNode.toString', () => {
  it('renders Equals/NotEquals with names when provided', () => {
    expect(new EqualsNode(0, 1, 'os', 'mac').toString()).toBe('os = mac');
    expect(new NotEqualsNode(1, 2, 'browser', 'ie').toString()).toBe('browser != ie');
  });

  it('falls back to index form when names are absent', () => {
    expect(new EqualsNode(0, 1).toString()).toBe('p0 = v1');
    expect(new NotEqualsNode(1, 2).toString()).toBe('p1 != v2');
  });

  it('renders composite nodes recursively', () => {
    const node = new ImpliesNode(
      new EqualsNode(0, 1, 'os', 'mac'),
      new NotEqualsNode(1, 2, 'browser', 'ie'),
    );
    expect(node.toString()).toBe('(os = mac IMPLIES browser != ie)');

    expect(new NotNode(new EqualsNode(0, 0, 'os', 'win')).toString()).toBe('NOT (os = win)');
    expect(
      new AndNode(
        new EqualsNode(0, 0, 'os', 'win'),
        new EqualsNode(1, 1, 'browser', 'firefox'),
      ).toString(),
    ).toBe('(os = win AND browser = firefox)');
    expect(
      new OrNode(
        new EqualsNode(0, 0, 'os', 'win'),
        new EqualsNode(1, 1, 'browser', 'firefox'),
      ).toString(),
    ).toBe('(os = win OR browser = firefox)');
    expect(
      new IfThenElseNode(
        new EqualsNode(0, 1, 'os', 'mac'),
        new NotEqualsNode(1, 2, 'browser', 'ie'),
        new NotEqualsNode(2, 0, 'arch', 'arm'),
      ).toString(),
    ).toBe('IF os = mac THEN browser != ie ELSE arch != arm');
  });

  it('renders IN, LIKE, relational and param comparisons', () => {
    expect(new InNode(0, [1, 2]).toString()).toBe('p0 IN {v1, v2}');
    expect(new LikeNode(0, 'chrome*', ['chrome', 'safari']).toString()).toBe('p0 LIKE chrome*');
    expect(RelationalNode.fromLiteral(0, RelOp.Greater, 3, ['1', '2', '3', '4']).toString()).toBe(
      'p0 > 3',
    );
    expect(RelationalNode.fromParams(0, RelOp.Less, 1, ['1', '2'], ['3', '4']).toString()).toBe(
      'p0 < p1',
    );
    expect(new ParamEqualsNode(0, 1, ['a'], ['a']).toString()).toBe('p0 = p1');
    expect(new ParamNotEqualsNode(0, 1, ['a'], ['b']).toString()).toBe('p0 != p1');
  });
});

describe('atom construction and evaluation cost', () => {
  function makeValues(count: number, length: number): string[] {
    return Array.from({ length: count }, (_, i) => 'v'.repeat(length) + i);
  }

  /** Run work `repetitions` times and return the fastest run, in milliseconds. */
  function fastestMs(repetitions: number, work: () => void): number {
    let best = Number.POSITIVE_INFINITY;
    for (let i = 0; i < repetitions; i++) {
      const start = performance.now();
      work();
      best = Math.min(best, performance.now() - start);
    }
    return best;
  }

  it('builds a LIKE node without redoing the pattern for every value', () => {
    // Both patterns fail on the first codepoint of every value, so matching
    // costs the same for either one and the only pattern-length-dependent work
    // left is decomposing the pattern. Decomposing it once per value instead of
    // once per node made the long pattern orders of magnitude slower to build.
    const values = makeValues(20000, 8);
    const shortPattern = 'z*';
    const longPattern = `${'z'.repeat(4000)}*`;

    const shortMs = fastestMs(3, () => {
      new LikeNode(0, shortPattern, values);
    });
    const longMs = fastestMs(3, () => {
      new LikeNode(0, longPattern, values);
    });

    expect(longMs).toBeLessThan(shortMs * 4 + 2);
  });

  it('evaluates a parameter comparison without looking at the value strings', () => {
    // Interning the values at construction is what makes these two runs cost
    // the same; comparing the strings themselves made the long-value run scale
    // with how long the values happen to be.
    const measure = (valueLength: number): number => {
      const node = new ParamEqualsNode(
        0,
        1,
        makeValues(64, valueLength),
        makeValues(64, valueLength),
      );
      const assignment = [0, 0];
      return fastestMs(5, () => {
        for (let i = 0; i < 200000; i++) {
          assignment[0] = i % 64;
          assignment[1] = (i * 7) % 64;
          node.evaluate(assignment);
        }
      });
    };

    const shortMs = measure(2);
    const longMs = measure(512);

    expect(longMs).toBeLessThan(shortMs * 4 + 5);
  });

  it('keeps the case-folding policy in the interned values', () => {
    const left = ['Alpha', 'beta'];
    const right = ['alpha', 'GAMMA'];

    expect(new ParamEqualsNode(0, 1, left, right).evaluate([0, 0])).toBe(True);
    expect(new ParamEqualsNode(0, 1, left, right, true).evaluate([0, 0])).toBe(False);
    expect(new ParamNotEqualsNode(0, 1, left, right).evaluate([0, 0])).toBe(False);
    expect(new ParamEqualsNode(0, 1, left, right).evaluate([1, 1])).toBe(False);
    expect(new ParamNotEqualsNode(0, 1, left, right).evaluate([1, 1])).toBe(True);
  });
});
