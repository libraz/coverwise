import { fileURLToPath } from 'node:url';
import ts from 'typescript';
import { beforeAll, describe, expect, it } from 'vitest';
import {
  analyzeCoverage,
  CoverwiseError,
  estimateModel,
  extendTests,
  generate,
  init,
  type Parameter,
  type TestCase,
  type WeightConfig,
} from '../../js/index.js';

/// Reading a field of a caller-supplied object runs the caller's code. A Vue
/// ref, a store proxy and a class instance with a computed property are all
/// ordinary things to hand to a test-design API, and any of them can throw from
/// a plain property read. Such a throw is a JavaScript exception: the compiled
/// module's `catch (const std::exception&)` does not recognise it, so it travels
/// out through the WebAssembly frames as-is.
///
/// What these assert is what a caller sees. Every entry point reports failure
/// as a CoverwiseError, for every caller-supplied object it reads, whether the
/// read that throws happens in the validation pass or inside the compiled
/// module — and the module is still usable afterwards, so one such input does
/// not cost the process its engine.

const CALLER_THREW = 'the caller refused the read';

const model = (): Parameter[] => [
  { name: 'n', values: ['a', 'b'] },
  { name: 'm', values: ['x', 'y'] },
];
const row = (): TestCase => ({ n: 'a', m: 'x' });
const weights = (): WeightConfig => ({ n: { a: 2 } });

/// Attaches the caller's code to `key` of a copy of `base`, the way a getter or
/// a proxy trap sits on a field of a store object.
type Attach = <T extends object>(base: T, key: string) => T;

/// What the planted read does when it runs. It answers with the value the field
/// held, or throws.
type Read = (held: unknown) => unknown;

/// The two shapes the same hazard arrives in: a getter, as a class instance or
/// a defineProperty-based ref has, and a trap, as a store proxy has.
const flavors: Array<{ name: string; attach: (read: Read) => Attach }> = [
  {
    name: 'a getter',
    attach: (read) => (base, key) => {
      const held = (base as Record<string, unknown>)[key];
      return Object.defineProperty({ ...base }, key, {
        enumerable: true,
        configurable: true,
        get: () => read(held),
      });
    },
  },
  {
    name: 'a proxy trap',
    attach: (read) => (base, key) =>
      new Proxy(base, {
        get: (target, property, receiver) =>
          property === key
            ? read(Reflect.get(target, property, receiver))
            : Reflect.get(target, property, receiver),
      }),
  },
];

/// One entry point driven with one caller-supplied object carrying the planted
/// read. `call` builds the input around `attach` and makes the call.
interface Case {
  entry: string;
  /// The caller-supplied object the read sits on.
  site: string;
  call: (attach: Attach) => unknown;
}

/// Every entry point, against every kind of object it reads out of its caller.
///
/// `analyzeCoverage` takes no weights map — its parameters, its rows and its
/// value objects are the whole of what it reads from the caller, so a value
/// object stands where the weights map stands for the other three.
const cases: Case[] = [
  {
    entry: 'generate',
    site: 'parameters',
    call: (attach) => generate(attach({ parameters: model() }, 'parameters')),
  },
  {
    entry: 'generate',
    site: 'weights',
    call: (attach) => generate(attach({ parameters: model(), weights: weights() }, 'weights')),
  },
  {
    entry: 'generate',
    site: 'a row',
    call: (attach) => generate({ parameters: model(), seeds: [attach(row(), 'n')] }),
  },
  {
    entry: 'analyzeCoverage',
    site: 'parameters',
    call: (attach) => analyzeCoverage([attach(model()[0], 'values'), model()[1]], [row()]),
  },
  {
    entry: 'analyzeCoverage',
    site: 'a value object',
    call: (attach) =>
      analyzeCoverage(
        [{ name: 'n', values: [attach({ value: 'a' }, 'value'), 'b'] }, model()[1]],
        [row()],
      ),
  },
  {
    entry: 'analyzeCoverage',
    site: 'a row',
    call: (attach) => analyzeCoverage(model(), [attach(row(), 'n')]),
  },
  {
    entry: 'extendTests',
    site: 'parameters',
    call: (attach) => extendTests([row()], attach({ parameters: model() }, 'parameters')),
  },
  {
    entry: 'extendTests',
    site: 'weights',
    call: (attach) =>
      extendTests([row()], attach({ parameters: model(), weights: weights() }, 'weights')),
  },
  {
    entry: 'extendTests',
    site: 'a row',
    call: (attach) => extendTests([attach(row(), 'n')], { parameters: model() }),
  },
  {
    entry: 'estimateModel',
    site: 'parameters',
    call: (attach) => estimateModel(attach({ parameters: model() }, 'parameters')),
  },
  {
    entry: 'estimateModel',
    site: 'weights',
    call: (attach) => estimateModel(attach({ parameters: model(), weights: weights() }, 'weights')),
  },
  {
    entry: 'estimateModel',
    site: 'a row',
    call: (attach) => estimateModel({ parameters: model(), seeds: [attach(row(), 'n')] }),
  },
];

/// A read that ran while the compiled module was on the stack, told apart by
/// the module's own glue frame: the validation pass reaches a field directly
/// from TypeScript, the module reaches it through emval.
function insideModule(stack: string | undefined): boolean {
  return stack !== undefined && (stack.includes('coverwise.js') || stack.includes('wasm://'));
}

function caught(call: () => unknown): unknown {
  try {
    call();
  } catch (thrown) {
    return thrown;
  }
  return undefined;
}

/// Fails the case unless the call reported failure the documented way.
function expectReported(thrown: unknown, label: string): void {
  expect(thrown, `${label}: the caller's throw was not converted`).toBeInstanceOf(CoverwiseError);
  const error = thrown as CoverwiseError;
  expect(error.code, label).toBe('INVALID_INPUT');
  // The caller's own text survives the conversion; an error that names nothing
  // is unactionable.
  expect(error.message, label).toContain(CALLER_THREW);
}

describe('input whose property reads run caller code', () => {
  beforeAll(async () => {
    await init();
  });

  for (const { entry, site, call } of cases) {
    describe(`${entry}, reading ${site}`, () => {
      for (const flavor of flavors) {
        it(`reports ${flavor.name} that throws as a CoverwiseError`, () => {
          const attach = flavor.attach(() => {
            throw new Error(CALLER_THREW);
          });
          expectReported(
            caught(() => call(attach)),
            `${entry}/${site}/${flavor.name}`,
          );
        });
      }

      it('reports a throw from every read of the field, the module reached included', () => {
        // A read that throws before the module is called is caught by the
        // validation pass, and asserting only that would leave the engine call
        // itself untested — so every read the field receives is made to throw
        // in turn, which covers the reads the module performs without this
        // having to know how many the validation pass performs first.
        const stacks: Array<string | undefined> = [];
        const counted = flavors[0].attach((held) => {
          stacks.push(new Error().stack);
          return held;
        });
        expect(
          caught(() => call(counted)),
          `${entry}/${site}: the plain input failed`,
        ).toBe(undefined);
        expect(stacks.length, `${entry}/${site}: the field was never read`).toBeGreaterThan(0);
        expect(
          stacks.filter(insideModule).length,
          `${entry}/${site}: the module never read the field`,
        ).toBeGreaterThan(0);

        for (let target = 1; target <= stacks.length; target += 1) {
          let read = 0;
          const attach = flavors[0].attach((held) => {
            read += 1;
            if (read === target) {
              throw new Error(CALLER_THREW);
            }
            return held;
          });
          expectReported(
            caught(() => call(attach)),
            `${entry}/${site}: read ${target}`,
          );
        }
      });

      it('leaves the engine usable', () => {
        const attach = flavors[0].attach(() => {
          throw new Error(CALLER_THREW);
        });
        void caught(() => call(attach));
        // An exception unwinding through the WebAssembly frames must not cost
        // the process its engine: the next caller gets a suite, not a corpse.
        const result = generate({ parameters: model() });
        expect(result.coverage).toBe(1);
        expect(result.tests.length).toBeGreaterThan(0);
      });
    });
  }

  describe('a throw that resists inspection', () => {
    // Describing the thrown value is the caller's code as well, so a value that
    // answers no question about itself must still leave a CoverwiseError rather
    // than a second foreign throw from the conversion itself.
    const thrownValues: Array<{ name: string; value: unknown }> = [
      { name: 'a bare string', value: CALLER_THREW },
      {
        name: 'an object that refuses every question',
        value: new Proxy(
          {},
          {
            get: () => {
              throw new Error('refused');
            },
            getPrototypeOf: () => {
              throw new Error('refused');
            },
          },
        ),
      },
    ];

    for (const { name, value } of thrownValues) {
      it(`reports ${name} as a CoverwiseError`, () => {
        const attach = flavors[0].attach(() => {
          throw value;
        });
        const thrown = caught(() => generate(attach({ parameters: model() }, 'parameters')));
        expect(thrown).toBeInstanceOf(CoverwiseError);
        expect((thrown as CoverwiseError).code).toBe('INVALID_INPUT');
        expect((thrown as CoverwiseError).message).toContain('Invalid input');
      });
    }
  });
});

/// The conversion is worth nothing if an entry point can reach the engine
/// beside it. What keeps that from happening is that the module handle is
/// private to the boundary — so these read the source and check exactly that,
/// rather than trusting that a later entry point will be written the same way.
describe('the boundary as the only route to the engine', () => {
  const indexPath = fileURLToPath(new URL('../../js/index.ts', import.meta.url));

  const source = ts.createSourceFile(
    indexPath,
    ts.sys.readFile(indexPath) ?? '',
    ts.ScriptTarget.Latest,
    true,
    ts.ScriptKind.TS,
  );

  interface NamedFunction {
    name: string;
    exported: boolean;
    body: ts.Node;
  }

  const isExported = (node: ts.Node): boolean =>
    ts.canHaveModifiers(node) &&
    (ts.getModifiers(node) ?? []).some((modifier) => modifier.kind === ts.SyntaxKind.ExportKeyword);

  function namedFunctions(): NamedFunction[] {
    const found: NamedFunction[] = [];
    for (const statement of source.statements) {
      if (ts.isFunctionDeclaration(statement) && statement.name && statement.body) {
        found.push({
          name: statement.name.text,
          exported: isExported(statement),
          body: statement.body,
        });
        continue;
      }
      if (ts.isClassDeclaration(statement)) {
        const className = statement.name?.text ?? 'class';
        for (const member of statement.members) {
          if (ts.isMethodDeclaration(member) && member.body) {
            found.push({
              name: `${className}.${member.name.getText(source)}`,
              exported: isExported(statement),
              body: member.body,
            });
          }
        }
        continue;
      }
      if (ts.isVariableStatement(statement)) {
        for (const declaration of statement.declarationList.declarations) {
          const initializer = declaration.initializer;
          if (
            ts.isIdentifier(declaration.name) &&
            initializer &&
            (ts.isArrowFunction(initializer) || ts.isFunctionExpression(initializer))
          ) {
            found.push({
              name: declaration.name.text,
              exported: isExported(statement),
              body: initializer.body,
            });
          }
        }
      }
    }
    return found;
  }

  function names(node: ts.Node, identifier: string): boolean {
    let found = false;
    const visit = (child: ts.Node): void => {
      if (found) {
        return;
      }
      if (ts.isIdentifier(child) && child.text === identifier) {
        found = true;
        return;
      }
      ts.forEachChild(child, visit);
    };
    visit(node);
    return found;
  }

  function calls(node: ts.Node, callee: string): boolean {
    let found = false;
    const visit = (child: ts.Node): void => {
      if (found) {
        return;
      }
      if (
        ts.isCallExpression(child) &&
        ts.isIdentifier(child.expression) &&
        child.expression.text === callee
      ) {
        found = true;
        return;
      }
      ts.forEachChild(child, visit);
    };
    visit(node);
    return found;
  }

  const functions = namedFunctions();

  it('reads the entry point source', () => {
    // Guards the enumeration: a rename that left this pointing at nothing would
    // make every assertion below vacuously true.
    expect(functions.map((entry) => entry.name)).toContain('callEngine');
  });

  it('keeps the module handle inside the boundary', () => {
    // Anything else that named the handle could call the engine without the
    // conversion; loading it and handing it out are the only two uses left.
    const holders = functions
      .filter((entry) => names(entry.body, 'wasmModule'))
      .map((entry) => entry.name)
      .sort();
    expect(holders).toEqual(['callEngine', 'init']);
  });

  it('routes every entry point that reaches the engine through it', () => {
    const routed = functions
      .filter((entry) => entry.exported && calls(entry.body, 'callEngine'))
      .map((entry) => entry.name)
      .sort();
    expect(routed).toEqual([...new Set(cases.map((one) => one.entry))].sort());
  });
});
