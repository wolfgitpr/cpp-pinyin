using System;
using System.IO;
using System.Runtime.Loader;
using System.Threading;
using Xunit.Runners;

internal static class Program
{
    private static int failures;
    private static int total;
    private static readonly ManualResetEventSlim Done = new ManualResetEventSlim(false);

    private static int Main(string[] args)
    {
        if (args.Length != 1)
        {
            Console.WriteLine("usage: Runner <path to the test assembly>");
            return 2;
        }

        // The default load context resolves only what this runner's own deps.json lists, so anything sitting
        // beside the test assembly -- starting with xunit's execution engine -- would not be found. Measured as
        // "Could not find/load any of the following assemblies: xunit.execution.dotnet.dll". Real runners ship a
        // merged dependency list; this is the same idea in six lines.
        AssemblyLoadContext.Default.Resolving += (context, name) =>
        {
            var candidate = Path.Combine(AppContext.BaseDirectory, name.Name + ".dll");
            return File.Exists(candidate) ? context.LoadFromAssemblyPath(candidate) : null;
        };

        var assembly = Path.GetFullPath(args[0]);
        var runner = AssemblyRunner.WithoutAppDomain(assembly);
        runner.OnTestFailed = info =>
        {
            Interlocked.Increment(ref failures);
            Console.WriteLine("FAIL  " + info.TestDisplayName);
            Console.WriteLine("      " + info.ExceptionMessage);
        };
        runner.OnTestSkipped = info => Console.WriteLine("SKIP  " + info.TestDisplayName);
        runner.OnTestFinished = info => Interlocked.Increment(ref total);
        runner.OnExecutionComplete = info => Done.Set();

        runner.Start();
        if (!Done.Wait(TimeSpan.FromMinutes(20)))
        {
            Console.WriteLine("TIMEOUT waiting for the run to finish");
            return 2;
        }

        Console.WriteLine("total " + total + ", failed " + failures);
        // NOT DISPOSED, ON PURPOSE. AssemblyRunner.Dispose() insists on its Idle status: on net7.0 the runner
        // is idle by the time OnExecutionComplete returns, on net10.0 it is not, and disposing anyway aborts
        // the process with
        //   System.InvalidOperationException: Cannot dispose the assembly runner when it's not idle
        // -- exit -6 and no summary line, which the gate read as "the C# shell's suite failed" (measured on
        // CI, ubuntu-latest + .NET 10.0.403, 2026-10-29; the same suite passed 75/75 under net7.0).
        // This runner IS the process: it prints one summary and exits, so the runner object needs no orderly
        // teardown. Terminate() flushes stdout and ends the runtime, so a runner thread still winding down
        // cannot hold the process open.
        //
        // Zero tests is a failure: a run that quietly discovered nothing would otherwise look like a pass.
        Terminate(failures == 0 && total > 0 ? 0 : 1);
        return 2;  // not reached: Terminate() ends the process. Present so the compiler sees every path return.
    }

    // Flushes what the callbacks wrote, then ends the process. Deliberately not [DoesNotReturn]: the attribute
    // would make the `return` above unreachable code, which this project builds as an error.
    private static void Terminate(int code)
    {
        Console.Out.Flush();
        // xunit's engine keeps threads of its own; the exit code must not wait for them.
        Environment.Exit(code);
    }
}
