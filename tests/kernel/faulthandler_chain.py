# register(chain=True) runs the previous action after the dump, whatever it is:
# here SIG_DFL, so the program dies of SIGUSR1 and "survived" never prints.
import faulthandler, os, signal, sys
faulthandler.register(signal.SIGUSR1, file=sys.stdout, all_threads=False, chain=True)
os.kill(os.getpid(), signal.SIGUSR1)
print("survived")
