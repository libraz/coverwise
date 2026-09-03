import { beforeAll, describe, expect, it } from 'vitest';

/// Reading a field of a caller-supplied object runs the caller's own code — a
/// getter, a Proxy trap, a computed member of a class instance — and what that
/// code throws is a JavaScript exception, not a C++ one. It is therefore not
/// the `std::exception` the compiled module catches, and it unwinds out through
/// the WebAssembly frames instead, past every destructor on the stack, to reach
/// the caller as a foreign throw from a function documented to return an error
/// object.
///
/// The package entry points convert such a throw for their own callers. An
/// embedder loading `dist/coverwise.js` gets none of that cover, so these drive
/// the compiled module directly and assert the property at its own boundary:
/// every export answers a caller-supplied object with a value or with
/// `{ error: true, code: 3 }`, whatever reading it runs, and the module is still
/// usable afterwards — one such input does not cost the embedder its engine.

interface RawResult {
  error?: true;
  code?: number;
  message?: string;
  tests?: Array<Record<string, string>>;
  coverageRatio?: number;
}

interface RawModule {
  generate(input: unknown): RawResult;
  analyzeCoverage(
    parameters: unknown,
    tests: unknown,
    strength: unknown,
    constraints: unknown,
  ): RawResult;
  extendTests(existing: unknown, input: unknown): RawResult;
  estimateModel(input: unknown): RawResult;
}

const INVALID_INPUT = 3;
const CALLER_THREW = 'the caller refused the read';

const model = () => [
  { name: 'n', values: ['a', 'b'] },
  { name: 'm', values: ['x', 'y'] },
];
const row = () => ({ n: 'a', m: 'x' });
const weights = () => ({ n: { a: 2 } });

let raw: RawModule;

/// Calls the module and fails if the call throws rather than returning.
function call(invoke: () => RawResult): RawResult {
  let result: RawResult | undefined;
  expect(() => {
    result = invoke();
  }).not.toThrow();
  return result as RawResult;
}

/// Attaches caller code to `key` of a copy of `base`, the way a getter or a
/// store proxy sits on a field of an ordinary object.
type Attach = <T extends object>(base: T, key: string) => T;

/// The two shapes the same hazard arrives in: a getter, as a class instance or
/// a `defineProperty`-based ref has, and a trap, as a store proxy has. Both are
/// made to throw; a value that reads cleanly is covered by the other files.
const flavors: Array<{ name: string; attach: Attach }> = [
  {
    name: 'a getter',
    attach: (base, key) =>
      Object.defineProperty({ ...base }, key, {
        enumerable: true,
        configurable: true,
        get: () => {
          throw new Error(CALLER_THREW);
        },
      }),
  },
  {
    name: 'a proxy trap',
    attach: (base, key) =>
      new Proxy(base, {
        get: (target, property, receiver) => {
          if (property === key) {
            throw new Error(CALLER_THREW);
          }
          return Reflect.get(target, property, receiver);
        },
      }),
  },
];

/// One export driven with one caller-supplied object carrying the planted read.
interface Case {
  entry: string;
  /// The caller-supplied object the read sits on.
  site: string;
  call: (attach: Attach) => RawResult;
}

/// Every export, against each kind of object it reads out of its caller.
///
/// `analyzeCoverage` takes no input object and no weights map — its parameters
/// and its rows are the whole of what it reads, so a parameter object stands
/// where the weights map stands for the other three.
const cases: Case[] = [
  {
    entry: 'generate',
    site: 'parameters',
    call: (attach) => raw.generate(attach({ parameters: model() }, 'parameters')),
  },
  {
    entry: 'generate',
    site: 'weights',
    call: (attach) => raw.generate(attach({ parameters: model(), weights: weights() }, 'weights')),
  },
  {
    entry: 'generate',
    site: 'a row',
    call: (attach) => raw.generate({ parameters: model(), seeds: [attach(row(), 'n')] }),
  },
  {
    entry: 'estimateModel',
    site: 'parameters',
    call: (attach) => raw.estimateModel(attach({ parameters: model() }, 'parameters')),
  },
  {
    entry: 'estimateModel',
    site: 'weights',
    call: (attach) =>
      raw.estimateModel(attach({ parameters: model(), weights: weights() }, 'weights')),
  },
  {
    entry: 'estimateModel',
    site: 'a row',
    call: (attach) => raw.estimateModel({ parameters: model(), seeds: [attach(row(), 'n')] }),
  },
  {
    entry: 'extendTests',
    site: 'parameters',
    call: (attach) => raw.extendTests([row()], attach({ parameters: model() }, 'parameters')),
  },
  {
    entry: 'extendTests',
    site: 'weights',
    call: (attach) =>
      raw.extendTests([row()], attach({ parameters: model(), weights: weights() }, 'weights')),
  },
  {
    entry: 'extendTests',
    site: 'a row',
    call: (attach) => raw.extendTests([attach(row(), 'n')], { parameters: model() }),
  },
  {
    entry: 'analyzeCoverage',
    site: 'a parameter object',
    call: (attach) =>
      raw.analyzeCoverage([attach(model()[0], 'values'), model()[1]], [row()], 2, []),
  },
  {
    entry: 'analyzeCoverage',
    site: 'a row',
    call: (attach) => raw.analyzeCoverage(model(), [attach(row(), 'n')], 2, []),
  },
];

/// Fails unless the call reported the failure instead of letting it out.
///
/// @param quoted Text the report must carry. The caller's own message by
///        default: a report that names nothing is unactionable, and it is what
///        the wrapper package quotes for a throw it caught before the module was
///        reached, so one input reads the same whichever surface read it.
function expectReported(result: RawResult, label: string, quoted: string = CALLER_THREW): void {
  expect(result.error, `${label}: the read that threw was not reported`).toBe(true);
  expect(result.code, label).toBe(INVALID_INPUT);
  expect(result.message, label).toContain(quoted);
  // No suite comes back from an input the module could not read: a row built
  // around a field it never obtained would be the module inventing a value.
  expect(result.tests, label).toBeUndefined();
}

describe('the compiled module reading input that runs caller code', () => {
  beforeAll(async () => {
    // @ts-expect-error — built by `yarn build:wasm`, aliased to dist/ in vitest.
    const createModule = await import('../coverwise.js');
    raw = (await createModule.default()) as RawModule;
  });

  for (const { entry, site, call: drive } of cases) {
    describe(`${entry}, reading ${site}`, () => {
      for (const { name, attach } of flavors) {
        it(`reports ${name} that throws`, () => {
          expectReported(
            call(() => drive(attach)),
            `${entry}/${site}/${name}`,
          );
        });
      }

      it('leaves the module usable', () => {
        call(() => drive(flavors[0].attach));
        const result = call(() => raw.generate({ parameters: model() }));
        expect(result.error).toBeUndefined();
        expect((result.tests as Array<Record<string, string>>).length).toBeGreaterThan(0);
      });
    });
  }

  /// A field read is not the only read of a caller-supplied object the module
  /// makes. It asks an array for its length and its elements, asks an object
  /// for its keys, and asks whether a key is present at all — each of which a
  /// proxy answers with code of its own.
  describe('reads that are not a field read', () => {
    const trapped = <T extends object>(target: T, trap: ProxyHandler<T>): T =>
      new Proxy(target, trap);
    const refuse = () => {
      throw new Error(CALLER_THREW);
    };

    it('reports an array that refuses its length', () => {
      const parameters = trapped(model(), {
        get: (target, property, receiver) =>
          property === 'length' ? refuse() : Reflect.get(target, property, receiver),
      });
      expectReported(
        call(() => raw.generate({ parameters })),
        'length',
      );
    });

    it('reports an array that refuses an element', () => {
      const values = trapped(['a', 'b'], {
        get: (target, property, receiver) =>
          property === '0' ? refuse() : Reflect.get(target, property, receiver),
      });
      expectReported(
        call(() => raw.generate({ parameters: [{ name: 'n', values }, model()[1]] })),
        'element',
      );
    });

    it('reports an object that refuses its keys', () => {
      const seeds = [trapped(row(), { ownKeys: refuse })];
      expectReported(
        call(() => raw.generate({ parameters: model(), seeds })),
        'ownKeys',
      );
    });

    it('reports an object that refuses a key-presence question', () => {
      // The boundary field parser asks whether a parameter declares a boundary
      // before it reads one, and a proxy answers that question too.
      const parameter = trapped(model()[0], { getOwnPropertyDescriptor: refuse });
      expectReported(
        call(() => raw.generate({ parameters: [parameter, model()[1]] })),
        'getOwnPropertyDescriptor',
      );
    });
  });

  /// A value read once is not a value read for good. `extendTests` keeps the
  /// rows it was handed exactly as supplied, so it reads that array a second
  /// time when it builds its result — and an object that answered the first
  /// read may answer the next one by throwing. Every probe that plants a throw
  /// on the first read passes without reaching this, which is what would make a
  /// wall with a gap in it look finished.
  describe('a read that succeeds once and throws the next time', () => {
    /// An array whose @p key answers normally until the @p nth read of it.
    const refusesOnRead = (key: string, nth: number) => {
      let reads = 0;
      return new Proxy([row()], {
        get(target, property, receiver) {
          if (property === key) {
            reads += 1;
            if (reads >= nth) {
              throw new Error(CALLER_THREW);
            }
          }
          return Reflect.get(target, property, receiver);
        },
      });
    };

    for (const key of ['length', '0']) {
      it(`reports a suite that refuses ${key} on the second read`, () => {
        expectReported(
          call(() => raw.extendTests(refusesOnRead(key, 2), { parameters: model() })),
          `existing/${key}`,
        );
      });
    }

    it('still echoes the rows it was handed when nothing refuses', () => {
      // The guard has to leave the echo itself intact: what makes the second
      // read worth making is that extend returns the caller's own rows.
      const existing = [{ n: 'a', m: 'x' }];
      const result = call(() => raw.extendTests(existing, { parameters: model() }));
      expect(result.error).toBeUndefined();
      expect((result.tests as Array<Record<string, string>>)[0]).toEqual(existing[0]);
    });
  });

  /// Quoting what was thrown runs the caller's code a second time, so the two
  /// values below are the ones that would turn the report itself into a foreign
  /// throw. Neither may cost the call its report: a value that answers no
  /// question about itself is described as one.
  describe('a thrown value that resists inspection', () => {
    const bare: unknown = CALLER_THREW;
    const hostile: unknown = new Proxy(
      {},
      {
        get: () => {
          throw new Error('refused');
        },
        getPrototypeOf: () => {
          throw new Error('refused');
        },
      },
    );

    for (const { name, value, quoted } of [
      { name: 'a bare string', value: bare, quoted: CALLER_THREW },
      // Not the text itself: what matters is that a report came back at all,
      // and it is the field the read was on that says where to look.
      { name: 'an object that refuses every question', value: hostile, quoted: 'parameters' },
    ]) {
      it(`reports ${name}`, () => {
        const input = Object.defineProperty({}, 'parameters', {
          enumerable: true,
          get: () => {
            throw value;
          },
        });
        expectReported(
          call(() => raw.generate(input)),
          name,
          quoted,
        );
        expect(call(() => raw.generate({ parameters: model() })).error).toBeUndefined();
      });
    }
  });
});
