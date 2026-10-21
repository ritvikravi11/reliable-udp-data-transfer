# Requirements traceability

## Evidence scope

The repository does not contain a separate rubric or assignment-instructions
file. The audit used:

- `CA1_Proposal_P3.pdf` supplied with the project.
- `Coding Assignment 1.pdf` supplied with the project.
- The current repository README and source tree.

The proposal is the project-specific requirements source. The assignment PDF
states the general gate: implement at socket level, produce measurable
evidence with experiments/plots/analysis, and be able to explain the design.

## Traceability matrix

| ID | Requirement | Implementation | Validation/evidence | Status |
|---|---|---|---|---|
| R1 | File transfer over raw UDP sockets | `src/file_transfer.c`; Winsock `socket`, `sendto`, `recvfrom`, `select` | Clean and impaired process integration tests | Implemented |
| R2 | Three swappable protocols | `src/stop_wait.c`, `src/go_back_n.c`, `src/selective_repeat.c` and headers | Unit tests and all three clean transfers | Implemented |
| R3 | Sequence numbers and checksums | `include/packet.h`, `src/packet.c` | `test_packet.exe`; corruption integration cases | Implemented |
| R4 | Cumulative ACKs for GBN | `src/go_back_n.c` | `tests/test_go_back_n.c`; loss/corruption integration cases | Implemented |
| R5 | Selective ACK behavior for SR | `src/selective_repeat.c` | `tests/test_selective_repeat.c`; reordering integration cases | Implemented |
| R6 | Retransmission timers | `include/timer.h`, `src/timer.c` | `test_timer.exe`, `test_timer_manager.exe`, protocol tests | Implemented |
| R7 | Jacobson/Karels RTO and Karn's algorithm | `src/timer.c`, `src/stop_wait.c` | `test_timer.exe` reports Karn PASS | Implemented; adaptive RTO coverage in GBN/SR is limited |
| R8 | Seeded loss, duplication, reordering, corruption, delay and jitter | `emulator/channel.c`, `emulator/udp_channel.c` | `test_channel.exe`; 21-case integration matrix | Implemented |
| R9 | Persistent bidirectional emulator | `emulator/udp_channel.c` | 21 successful sender/emulator/receiver transfers | Implemented |
| R10 | File integrity by hash comparison | `src/sha256.c`, `src/file_transfer.c`, test runner | SHA-256 equality required by integration runner | Implemented |
| R11 | Exact 10 MB loss sweep, 0–30% in 5% steps | `scripts/run_experiments.py` | `results/final/loss_sweep.csv` and manifest | Pending final run |
| R12 | Goodput-vs-loss plot for all protocols | `scripts/plot_results.py` | `results/final/plots/goodput_vs_loss_rate.svg` | Pending final run |
| R13 | Goodput-vs-window-size plot | `scripts/run_experiments.py`, `scripts/plot_results.py` | `results/final/window_sweep.csv`, plot | Pending final run |
| R14 | Retransmissions-vs-reordering plot | `scripts/run_experiments.py`, `scripts/plot_results.py` | `results/final/reorder_sweep.csv`, plot | Pending final run |
| R15 | Repeated trials, means and variability | `scripts/run_experiments.py`, final analysis | Final CSV and analysis tables | Pending final run |
| R16 | Results interpretation and reproducibility instructions | `results/final/RESULTS_ANALYSIS.md`, README | Final results package | Pending final run |
| R17 | AI-use disclosure and viva preparation | `AI-USE.md`, proposal work split | Repository documentation | Implemented/documented |

## Assignment-level requirements

The assignment PDF requires a from-scratch socket-level implementation,
measurable evidence, plots/analysis, and explainable design. R1–R10 cover the
implementation requirement. R11–R16 cover the evidence requirement. The
individual viva requirement cannot be verified by repository execution.

## Known limitations

- The proposal does not specify a numerical repetition count. The project
  uses the configured final plan of three repetitions and records every run.
- The proposal requests adaptive RTO across all protocols. The shared timer
  subsystem is tested, but the current GBN/SR APIs use a fixed default RTO.
- No separate rubric was available in the repository or supplied files.
- Full matrix status is updated only after the final commands complete; this
  document intentionally does not pre-claim those results.
