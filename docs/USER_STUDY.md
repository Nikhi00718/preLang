# User study protocol: lifting a pandas cleaning script

A small study of practical usefulness: do people who already write pandas get something from
PrepLang on their own cleaning code? Results may only come from real participants; nothing in the
paper may report this study until it has been run.

## Question

When a participant's own pandas cleaning script is lifted with `frontend/pandas2prep.py` and
compiled, does PrepLang report a problem the participant did not know about, and how long does it
take to get a compiled program?

## Participants

5 to 8 students who have written at least one pandas cleaning script (a course assignment, a Kaggle
notebook, a project). Participation is voluntary, takes about 30 minutes, and can be stopped at any
time. Record no names in the results: use P1, P2, ...

## Materials

- The Docker image (`docker build -t preplang .`) or a machine with the compiler built.
- Each participant brings one script and its data (CSV or Parquet). If they have none, they pick one
  of the Titanic scripts in `eval/data/titanic` they have not seen.
- This sheet, printed, to note answers.

## Procedure (per participant)

1. **Background (2 min).** Ask: years of Python, how often they write pandas, whether they have used
   a data-validation tool (pandera, Great Expectations, TFDV).
2. **Lift (5 min).** Run `python frontend/pandas2prep.py their_script.py -o lifted.prep`. Note the
   statements lifted and where the lifter stopped (it prints `lifted until line N`).
3. **Compile (5 min).** Run `preplang lifted.prep --explain`. Note every error and warning.
4. **Classify (10 min).** For each error or warning, ask the participant: did you know about this?
   Is it a real problem in your script or data? Record one of: *real, unknown before*; *real, already
   known*; *not a problem* (a false alarm); *not sure*.
5. **Reuse (5 min, if they have a second batch of the same data).** Run the generated program on it
   with `name=file`. Note whether it was admitted, deoptimized something, or refused, and whether the
   participant agrees with that outcome.
6. **Questions (3 min).** Was any message hard to understand? Would you use this on your next
   cleaning script, and for what?

## What to record

| Field | Value |
|---|---|
| participant | P1 |
| script source | course / Kaggle / project / corpus |
| pandas lines | |
| statements lifted / lifter stopped at | |
| compile time (s) | |
| errors and warnings | count |
| real, unknown before | count |
| real, already known | count |
| false alarms | count |
| second batch outcome | admitted / deoptimized / refused / not tried |
| would use again | yes / maybe / no, and why |

## Reporting

Report counts per participant and totals, every false alarm with its message, and the participants'
own words for the last question. With 5 to 8 participants this is a qualitative study: no
percentages beyond counts, and no claim about developers in general.
