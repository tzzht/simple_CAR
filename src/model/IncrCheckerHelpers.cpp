#include "IncrCheckerHelpers.h"

namespace car {

Branching::Branching(int type) {
    branching_type = type;
    conflict_index = 1;
    mini = 1 << 20;
    counts.clear();
}


Branching::~Branching() {}


void Branching::Update(const shared_ptr<cube> uc) {
    if (uc->size() == 0) return;
    conflict_index++;
    switch (branching_type) {
    case 1: {
        Decay();
        break;
    }
    case 2: {
        if (conflict_index == 256) {
            for (int i = mini; i < counts.size(); i++)
                counts[i] *= 0.5;
            conflict_index = 0;
        }
        break;
    }
    }
    // assumes cube is ordered
    int sz = abs(uc->back());
    if (sz >= counts.size()) counts.resize(sz + 1);
    if (mini > abs(uc->at(0))) mini = abs(uc->at(0));
    for (auto l : *uc) {
        switch (branching_type) {
        case 1:
        case 2: {
            assert(abs(l) < counts.size());
            counts[abs(l)]++;
            break;
        }
        case 3:
            counts[abs(l)] = (counts[abs(l)] + conflict_index) / 2.0;
            break;
        }
    }
}


void Branching::Decay() {
    for (int i = mini; i < counts.size(); i++)
        counts[i] *= 0.99;
}


void Branching::Decay(const shared_ptr<cube> uc, int gap = 1) {
    if (uc->size() == 0) return;
    conflict_index++;
    // assumes cube is ordered
    int sz = abs(uc->back());
    if (sz >= counts.size()) counts.resize(sz + 1);
    if (mini > abs(uc->at(0))) mini = abs(uc->at(0));
    for (auto l : *uc) {
        counts[abs(l)] *= 1 - 0.01 * (gap - 1);
    }
}


static bool _cmp(int a, int b) {
    if (abs(a) != abs(b))
        return abs(a) < abs(b);
    else
        return a < b;
}


// ================================================================================
// @brief: if a implies b
// @input:
// @output:
// ================================================================================
bool OverSequenceSet::is_imply(shared_ptr<cube> a, shared_ptr<cube> b) {
    if (a->size() > b->size())
        return false;
    if (includes(b->begin(), b->end(), a->begin(), a->end(), _cmp))
        return true;
    else
        return false;
}


void OverSequenceSet::add_uc_to_frame(const shared_ptr<cube> uc, shared_ptr<frame> f) {
    frame tmp;
    for (auto f_uc : *f) {
        if (!is_imply(uc, f_uc))
            tmp.emplace(f_uc);
    }
    tmp.emplace(uc);
    f->swap(tmp);
}


bool OverSequenceSet::Insert(shared_ptr<cube> uc, int index, bool need_imply) {
    m_blockSolver->AddUC(uc, index);
    if (index >= m_sequence.size()) {
        shared_ptr<frame> new_frame(new frame);
        m_sequence.emplace_back(new_frame);
        m_blockCounter.emplace_back(0);
    }
    auto res = m_UCSet.insert(uc);
    if (!need_imply)
        m_sequence[index]->emplace(*res.first);
    else
        add_uc_to_frame(*res.first, m_sequence[index]);
    return true;
}


bool OverSequenceSet::IsBlockedByFrame(shared_ptr<cube> latches, int frameLevel) {
    if (frameLevel >= m_sequence.size()) return false;
    // by for checking
    int latch_index, num_inputs;
    num_inputs = m_model->GetNumInputs();
    for (auto uc : *m_sequence[frameLevel]) { // for each uc
        bool blocked = true;
        for (int j = 0; j < uc->size(); j++) { // for each literal
            latch_index = abs(uc->at(j)) - num_inputs - 1;
            if (latches->at(latch_index) != uc->at(j)) {
                blocked = false;
                break;
            }
        }
        if (blocked) {
            return true;
        }
    }
    return false;
}


bool OverSequenceSet::IsBlockedByFrame_sat(shared_ptr<cube> latches, int frameLevel) {
    if (frameLevel >= m_sequence.size()) return false;
    bool result = m_blockSolver->SolveFrame(latches, frameLevel);
    if (!result) {
        return true;
    } else {
        return false;
    }
}


bool OverSequenceSet::IsBlockedByFrame_lazy(shared_ptr<cube> latches, int frameLevel) {
    if (frameLevel >= m_sequence.size()) return false;
    int &counter = m_blockCounter[frameLevel];
    if (counter == -1) { // by sat
        bool result = m_blockSolver->SolveFrame(latches, frameLevel);
        if (!result) {
            return true;
        } else {
            return false;
        }
    }
    if (m_sequence[frameLevel]->size() > 3000) {
        counter++;
    }
    // whether it's need to change the way of checking
    clock_t start_time, sat_time, for_time;
    if (counter > 1000) {
        start_time = clock();
        m_blockSolver->SolveFrame(latches, frameLevel);
        sat_time = clock();
    }
    // by imply checking
    for (auto uc : *m_sequence[frameLevel]) { // for each uc
        if (includes(latches->begin(), latches->end(), uc->begin(), uc->end(), _cmp)) return true;
    }
    if (counter > 1000) {
        for_time = clock();
        if (sat_time - start_time > for_time - sat_time)
            counter = 0;
        else
            counter = -1;
    }
    return false;
}


void OverSequenceSet::GetBlockers(shared_ptr<cube> c, int framelevel, vector<shared_ptr<cube>> &b) {
    if (framelevel >= m_sequence.size()) return;
    int size = -1;
    // by imply checking
    for (auto uc : *m_sequence[framelevel]) { // for each uc
        if (size != -1 && size < uc->size()) break;
        if (includes(c->begin(), c->end(), uc->begin(), uc->end(), _cmp)) {
            size = uc->size();
            b.emplace_back(uc);
        }
    }
}


string OverSequenceSet::FramesInfo() {
    string res;
    res += "Frames " + to_string(m_sequence.size() - 1) + "\n";
    for (int i = 0; i < m_sequence.size(); ++i) {
        res += to_string(m_sequence[i]->size()) + " ";
    }
    return res;
}


string OverSequenceSet::FramesDetail() {
    string res;
    for (int i = 0; i < m_sequence.size(); ++i) {
        res += "Frame " + to_string(i) + "\n";
        if (i != 0) {
            for (auto uc : *m_sequence[i]) {
                for (auto j : *uc) {
                    res += to_string(j) + " ";
                }
                res += "\n";
            }
        }
        res += "size: " + to_string(m_sequence[i]->size()) + "\n";
    }
    return res;
}


int State::numInputs = -1;
int State::numLatches = -1;


string State::GetLatchesString() {
    string result = "";
    result.reserve(numLatches);
    int j = 0;
    for (int i = 0; i < numLatches; ++i) {
        if (j >= latches->size() || numInputs + i + 1 < abs(latches->at(j))) {
            result += "x";
        } else {
            result += (latches->at(j) > 0) ? "1" : "0";
            ++j;
        }
    }
    return result;
}


string State::GetInputsString() {
    string result = "";
    result.reserve(numInputs);
    int j = 0;
    for (int i = 1; i <= numInputs; ++i) {
        if (j >= inputs->size() || i < abs(inputs->at(j))) {
            result += "x";
        } else {
            result += (inputs->at(j) > 0) ? "1" : "0";
            ++j;
        }
    }
    return result;
}


} // namespace car