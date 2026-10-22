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
        using (var runner = AssemblyRunner.WithoutAppDomain(assembly))
        {
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
        }

        Console.WriteLine("total " + total + ", failed " + failures);
        // Zero tests is a failure: a run that quietly discovered nothing would otherwise look like a pass.
        return failures == 0 && total > 0 ? 0 : 1;
    }
}
