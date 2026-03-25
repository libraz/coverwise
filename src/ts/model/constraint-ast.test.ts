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
