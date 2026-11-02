"""Target-side adapter: retain upstream Run.measure and answer verification."""
import sys

sys.path.insert(0, sys.argv[1])
from run import Run


class BudgetRun(Run):
    def _do_runs(self, bench):
        # Stop between verified iterations, without killing a benchmark mid-check.
        for completed in range(1, 1001):
            self.measure(bench)
            if completed >= 20 and self._total >= 10_000_000:
                break
        self._num_iterations = completed


run = BudgetRun(sys.argv[2])
run.set_inner_iterations(int(sys.argv[3]))
run.run_benchmark()
run.print_total()
