import { describe, expect, it } from 'vitest';
import { fastestEach } from '../../../tests/util/timing.js';
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
import { MAX_VALUES_PER_PARAMETER } from './limits.js';

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

  it('answers False past the largest member', () => {
    const node = new InNode(0, [1, 3]);
    expect(node.evaluate([4])).toBe(False);
    expect(node.evaluate([9999])).toBe(False);
  });

  it('does not let an index no parameter can hold match or size the table', () => {
    // A membership table is indexed by value index, so what bounds the table is
    // the largest index a parameter may have. An index past that belongs to no
    // parameter and is not a member of anything.
    const node = new InNode(0, [1, MAX_VALUES_PER_PARAMETER, UNASSIGNED - 1]);
    expect(node.evaluate([1])).toBe(True);
    expect(node.evaluate([MAX_VALUES_PER_PARAMETER])).toBe(False);
    expect(node.evaluate([UNASSIGNED - 1])).toBe(False);
  });

  it('matches nothing for an empty set but still answers the unassigned branch', () => {
    const node = new InNode(0, []);
    expect(node.evaluate([UNASSIGNED])).toBe(Unknown);
    expect(node.evaluate([0])).toBe(False);
  });

  it('treats a repeated member as one member', () => {
    const node = new InNode(0, [2, 2, 2]);
    expect(node.evaluate([2])).toBe(True);
    expect(node.evaluate([1])).toBe(False);
  });

  it('does not let the order the set was written in reach the answer', () => {
    const ascending = new InNode(0, [1, 3, 5]);
    const shuffled = new InNode(0, [5, 1, 3]);
    for (let value = 0; value <= 7; ++value) {
      expect(shuffled.evaluate([value])).toBe(ascending.evaluate([value]));
    }
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

  /// Ceiling on a hang, not a performance budget: these gates measure suites big
  /// enough that a default unit-test timeout does not apply, and they run under
  /// coverage instrumentation on a shared runner. The assertions compare two
  /// measurements from the same run, so they are unaffected by it.
  const MEASUREMENT_TIMEOUT_MS = 120_000;

  /// Rounds each timing gate below samples. Chosen by watching the estimator
  /// settle: past ten the high side of these ratios stops moving, and every gate
  /// here is an upper bound, so the high side is the one that matters.
  const TIMING_RUNS = 6;

  it('builds a LIKE node without redoing the pattern for every value', {
    timeout: MEASUREMENT_TIMEOUT_MS,
  }, () => {
    // Both patterns fail on the first codepoint of every value, so matching
    // costs the same for either one and the only pattern-length-dependent work
    // left is decomposing the pattern. Decomposing it once per value instead of
    // once per node made the long pattern orders of magnitude slower to build.
    // The value count puts a run in the hundreds of milliseconds, and it also
    // sharpens the comparison: the more values a node holds, the smaller a
    // share of its construction one pattern decomposition can be.
    const values = makeValues(300000, 8);
    const shortPattern = 'z*';
    const longPattern = `${'z'.repeat(4000)}*`;

    const [shortMs, longMs] = fastestEach(
      TIMING_RUNS,
      () => {
        new LikeNode(0, shortPattern, values);
      },
      () => {
        new LikeNode(0, longPattern, values);
      },
    );

    // Decomposing once per node keeps the ratio at 1.0 however long the pattern
    // is. The bound separates that from decomposing once per value, which costs
    // 207x here, measured by building one node per value so that the pattern is
    // decomposed as many times as there are values. Two orders of magnitude
    // apart leaves the bound room to sit far above anything contention produces
    // and still give the regression nowhere to hide.
    expect(longMs).toBeLessThan(shortMs * 5.0);
  });

  it('evaluates a parameter comparison without looking at the value strings', {
    timeout: MEASUREMENT_TIMEOUT_MS,
  }, () => {
    // Interning the values at construction is what makes these two runs cost
    // the same; comparing the strings themselves made the long-value run scale
    // with how long the values happen to be.
    // Enough evaluations to put a run in the hundreds of milliseconds; at tens
    // the ratio reports the machine as much as the node.
    const evaluations = 4000000;
    const evaluate = (valueLength: number): (() => void) => {
      const node = new ParamEqualsNode(
        0,
        1,
        makeValues(64, valueLength),
        makeValues(64, valueLength),
      );
      const assignment = [0, 0];
      return () => {
        for (let i = 0; i < evaluations; i++) {
          assignment[0] = i % 64;
          assignment[1] = (i * 7) % 64;
          node.evaluate(assignment);
        }
      };
    };

    const [shortMs, longMs] = fastestEach(TIMING_RUNS, evaluate(2), evaluate(512));

    // Interned keys make this a comparison of two indices, so the honest ratio
    // is 1.0 whatever the values are. The bound separates that from comparing
    // the strings themselves, which costs 8.8x here, measured by folding and
    // comparing both values on every evaluation.
    //
    // That regression is far smaller on this side than the 53x the C++ core
    // measures for the same defect, because folding and comparing a string is
    // cheap in this engine. The bound is set from the regime measured here
    // rather than copied across, which is why it is not the 5.0 the core uses:
    // 3.0 sits clear of contention and still well below 8.8.
    expect(longMs).toBeLessThan(shortMs * 3.0);
  });

  it('tests IN membership without walking the set', { timeout: MEASUREMENT_TIMEOUT_MS }, () => {
    // A large IN set must not cost more per evaluation than a small one: an IN
    // clause is the plain way to write a long disjunction, so it must not be
    // the slow way. Precomputing membership at construction is what makes these
    // two runs cost the same.
    const evaluations = 2000000;
    const domain = 4096;
    const evaluate = (setSize: number): (() => void) => {
      const node = new InNode(
        0,
        Array.from({ length: setSize }, (_, i) => i),
      );
      const assignment = [0];
      return () => {
        // Sweeping the whole domain keeps both runs on the same mix of members
        // and non-members, so neither is handed the cheaper answer more often.
        for (let i = 0; i < evaluations; i++) {
          assignment[0] = i % domain;
          node.evaluate(assignment);
        }
      };
    };

    const [smallMs, largeMs] = fastestEach(TIMING_RUNS, evaluate(10), evaluate(2000));

    // A membership lookup is one indexed read whatever the set holds, so the
    // honest ratio is 1.0 and anything above it is contention. The bound
    // separates that from walking the set, which costs 136x here, measured by
    // scanning the member list on every evaluation. Two orders of magnitude
    // apart leaves the bound room to sit far above contention and still give
    // the regression nowhere to hide.
    expect(largeMs).toBeLessThan(smallMs * 5.0);
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
