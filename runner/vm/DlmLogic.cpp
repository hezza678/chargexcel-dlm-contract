#include "DlmLogic.h"

#include <cstring>

void DlmLogic::sanitiseAction(const char* in, char* out, size_t capacity)
{
    if (!out || capacity == 0)
        return;
    size_t n = 0;
    const size_t limit = capacity - 1 < DLM_ACTION_MAX ? capacity - 1 : DLM_ACTION_MAX;
    for (; in && in[n] != '\0' && n < limit; ++n)
    {
        const unsigned char c = static_cast<unsigned char>(in[n]);
        out[n] = (c >= 0x20 && c <= 0x7E) ? static_cast<char>(c) : '?';
    }
    out[n] = '\0';
}

DlmDecision DlmLogic::record(
    const DlmAdvisory& advisory, const DlmProviderState& providerState, int64_t nowUs)
{
    DlmDecision decision;
    decision.providerState = providerState;

    // Rate limit first: a flooding plugin is refused before anything is
    // copied, and the previous submission stays on the page.
    if (providerState.lastSubmissionUs != 0 &&
        nowUs - providerState.lastSubmissionUs < MIN_SUBMISSION_INTERVAL_US)
    {
        decision.verdict = DlmVerdict::TooFrequent;
        ++decision.providerState.tooFrequentCount;
        return decision;
    }

    decision.verdict = DlmVerdict::Recorded;
    decision.providerState.lastSubmissionUs = nowUs;
    decision.providerState.present = advisory.present;
    sanitiseAction(advisory.action, decision.providerState.action, sizeof(decision.providerState.action));
    ++decision.providerState.recordedCount;
    return decision;
}

bool DlmLogic::online(const DlmProviderState& state, int64_t nowUs)
{
    return state.lastSubmissionUs != 0 && nowUs - state.lastSubmissionUs < OFFLINE_AFTER_US;
}
