"""mako's render_table inner loop as a ladder. Usage: mako_cell.py CASE WARM REPS
Each rep renders one 150x150 table into a fresh deque, as a render does."""
import sys, collections
sys.modules['markupsafe'] = None
from mako import filters

TABLE = [range(150) for i in range(150)]

def l0(w, table):
    for row in table:
        for col in row:
            pass

def l1(w, table):
    for row in table:
        for col in row:
            w('            <td>')

def l2(w, table):
    for row in table:
        for col in row:
            w('            <td>')
            w(str(col))

def l3(w, table):
    for row in table:
        for col in row:
            w('            <td>')
            w(filters.html_escape(str(col)))

def l4(w, table):
    for row in table:
        for col in row:
            w('            <td>')
            w(filters.html_escape(str(col)))
            w('</td>\n')

def main():
    f = globals()[sys.argv[1]]
    warm, reps = int(sys.argv[2]), int(sys.argv[3])
    for i in range(warm + reps):
        f(collections.deque().append, TABLE)

main()
