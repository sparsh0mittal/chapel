/*
This is a test of Chapel's write() of a string vs. C's printf()
of the same string, which has no formatting in it.

The test exposes Chapel's overhead on top of the actual work
of displaying the string.

To make the timing meaningful, the test performs write()/printf()
multiple times. To reduce the output volume, we use a short string.
This has the (unintentional) effect of emphasizing the overhead.

Since we expect the output to be redirected, due to its high volume,
there may be various startup effects.
To mitigate them, we rotate the variants 'tries' times,
starting with the one we expect to be the fastest.
However, the initial measurements did not support this concern.

Test variations - in C:
* Use printf() or puts().
* For C, wrap printf() in a loop in C or in Chapel.
* Pass "\n" to printf() from Chapel as a Chapel 'string' or 'c_string'.
* For the previous one, make it a global constant or a local constant.
* We could also do the above variations for puts() - skip that for now,
  to reduce the amount of info to deal with, and perhaps puts() is
  less interesting.

Test variations - in Chapel:
* since stdout is blocking, we also try a variant using a workaround of
  creating a non-blocking version of it - see stdoutNB.
*/

use Time;

config const n = 3;
config const tries = 2;
config const fmt = "%7.3dr";
config const compareTimes = false;

// Each trial() function writes a newline 'n' times.

//////////////////////////////////////////////////////////////////

extern proc cf_trial(n: int);
extern proc cs_trial(n: int);

proc lstr_trial() {
  extern proc printf(format: string);
  for 1..n do
    printf("\n");
}

proc lcs_trial() {
  extern proc printf(format: c_string);
  const c_newline_local = "\n".c_str();
  for 1..n do
    printf(c_newline_local);
}

const str_newline_global = "\n";
proc gstr_trial() {
  extern proc printf(format: string);
  for 1..n do
    printf(str_newline_global);
}

const c_newline_global = "\n".c_str();
proc gcs_trial() {
  extern proc printf(format: c_string);
  for 1..n do
    printf(c_newline_global);
}

// could do the same with puts() - would be too much, skip it for now

//////////////////////////////////////////////////////////////////

// write() a newline to stdout (non-blocking), 'n' times
const outfd = openfd(1);
const stdoutNB = outfd.writer(locking=false);
proc nb_trial() {
  for 1..n do
    stdoutNB.write("\n");
}

// write() a newline to stdout (blocking), 'n' times
proc sto_trial() {
  for 1..n do
    write("\n");
}

//////////////////////////////////////////////////////////////////

var tSave, tDummy, tcf, tcs, tlstr, tlcs, tgstr, tgcs, tnb, tsto: real;

inline proc addTime(ref t: real) {
  const tCurr = getCurrentTime();
  t += tCurr - tSave;
  tSave = tCurr;

}

const reportFormat = "%-24s " + fmt + "\n";
inline proc reportTime(title: string, time: real) {
  stderr.writef(reportFormat, title, time);
}

stderr.writef("n      %i\n", n);
stderr.writef("tries  %i\n", tries);

for t in 1..tries {
  stderr.writef("starting try %i\n", t);
  addTime(tDummy);

  cf_trial(n);   addTime(tcf);
  cs_trial(n);   addTime(tcs);
  lstr_trial();  addTime(tlstr);
  lcs_trial();   addTime(tlcs);
  gstr_trial();  addTime(tgstr);
  gcs_trial();   addTime(tgcs);

  nb_trial();     addTime(tnb);
  sto_trial();   addTime(tsto);
}

stderr.writef("done tries\n");
//stderr.writef("n      %i\n", n);
//stderr.writef("tries  %i\n", tries);

if compareTimes {
  reportTime("C printf", tcf);
  reportTime("C puts", tcs);
  reportTime("C/chpl string literal",  tlstr);
  reportTime("C/chpl c_string local",  tlcs);
  reportTime("C/chpl string global",   tgstr);
  reportTime("C/chpl c_string global", tgcs);
  reportTime("chpl non-blocking", tnb);
  reportTime("chpl stdout", tsto);
}