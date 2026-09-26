// The capture script compiles this wrapper inside the pinned oracle worktree.
// The legacy source tree supplies the production snapshot/render functions;
// this wrapper only keeps the artifact CLI stable across the two builds.
#define SPECTRUM_EXPORTER_NO_MAIN
#include "../spectrum_candidate_export.cpp"

int main(int argc, char** argv) { return spectrumCandidateExportMain(argc, argv); }
