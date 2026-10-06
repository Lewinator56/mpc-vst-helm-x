#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <cmath>
#include <vector>
#include <string>
#include <map>
#include <mutex>
#include <dirent.h>
#include <sys/stat.h>
#include <fstream>
#include <sstream>
#include <algorithm>

#include "helm_engine.h"
#include "helm_common.h"
#include "simple_json.h"

extern "C" {
#if __has_include("engine.h")
#include "engine.h"
#elif __has_include("../../mpc-vst-plugins/wrapper/engine.h")
#include "../../mpc-vst-plugins/wrapper/engine.h"
#else
typedef struct {
    void *(*create)(const char *data_dir);
    void (*destroy)(void *inst);
    void (*midi)(void *inst, const uint8_t *msg, int len);
    void (*set_param)(void *inst, const char *key, const char *val);
    int (*get_param)(void *inst, const char *key, char *buf, int buf_len);
    void (*render)(void *inst, int16_t *out_lr, int frames);
    void (*process)(void *inst, const int16_t *in_lr, int16_t *out_lr, int frames);
} mpc_engine_t;
const mpc_engine_t *mpc_engine(void);
#endif
}

namespace {

static const char* const MOD_SOURCES[] = {
    "",
    "mono_lfo_1",
    "mono_lfo_2",
    "poly_lfo",
    "step_sequencer",
    "amp_envelope",
    "fil_envelope",
    "mod_envelope",
    "random",
    "velocity",
    "note",
    "mod_wheel",
    "pitch_wheel",
    "aftertouch"
};
static const int NUM_MOD_SOURCES = sizeof(MOD_SOURCES) / sizeof(MOD_SOURCES[0]);

static const char* const MOD_DESTS[] = {
    "",
    "cutoff",
    "resonance",
    "filter_drive",
    "filter_blend",
    "fil_env_depth",
    "osc_1_waveform",
    "osc_1_transpose",
    "osc_1_tune",
    "osc_1_volume",
    "osc_1_pan",
    "osc_1_unison_detune",
    "osc_1_unison_spread",
    "osc_2_waveform",
    "osc_2_transpose",
    "osc_2_tune",
    "osc_2_volume",
    "osc_2_pan",
    "osc_2_unison_detune",
    "osc_2_unison_spread",
    "cross_modulation",
    "osc_feedback_amount",
    "sub_waveform",
    "sub_shuffle",
    "sub_volume",
    "noise_volume",
    "volume",
    "distortion_drive",
    "distortion_mix",
    "delay_frequency",
    "delay_feedback",
    "delay_dry_wet",
    "reverb_feedback",
    "reverb_damping",
    "reverb_dry_wet",
    "stutter_frequency",
    "stutter_softness",
    "formant_x",
    "formant_y",
    "mono_lfo_1_frequency",
    "mono_lfo_1_amplitude",
    "mono_lfo_2_frequency",
    "mono_lfo_2_amplitude",
    "poly_lfo_frequency",
    "poly_lfo_amplitude",
    "step_frequency",
    "amp_attack",
    "amp_decay",
    "amp_sustain",
    "amp_release",
    "fil_attack",
    "fil_decay",
    "fil_sustain",
    "fil_release",
    "mod_attack",
    "mod_decay",
    "mod_sustain",
    "mod_release"
};
static const int NUM_MOD_DESTS = sizeof(MOD_DESTS) / sizeof(MOD_DESTS[0]);

static const int NUM_MOD_SLOTS = 8;

static float get_dest_modulation_scale(int dest_idx) {
    if (dest_idx <= 0 || dest_idx >= NUM_MOD_DESTS) return 1.0f;
    const char* dst_name = MOD_DESTS[dest_idx];
    if (!dst_name || !*dst_name) return 1.0f;
    if (!mopo::Parameters::isParameter(dst_name)) return 1.0f;
    const auto& d = mopo::Parameters::getDetails(dst_name);
    if (d.min < 0.0f && d.max > 0.0f) {
        return (float)d.max;
    }
    return (float)(d.max - d.min);
}

struct ModSlot {
    int source_idx = 0;
    int dest_idx = 0;
    float amount = 0.0f;
    mopo::ModulationConnection* connection = nullptr;
};

struct PatchEntry {
    std::string path;
    std::string name;
    std::string folder;
    std::string author;
};

inline int16_t float_to_int16(float v) {
    float s = v * 32767.0f;
    if (s > 32767.0f) return 32767;
    if (s < -32768.0f) return -32768;
    return (int16_t)std::lrintf(s);
}

} // namespace

struct HelmInstance {
    std::mutex mtx;
    mopo::HelmEngine engine;
    mopo::control_map controls;
    mopo::ModulationConnectionBank mod_bank;

    ModSlot mod_slots[NUM_MOD_SLOTS];

    std::vector<PatchEntry> patches;
    std::vector<std::string> categories;
    int current_category = 0;
    std::vector<int> filtered_indices;
    int current_preset = 0;
    std::string current_patch_name = "Init";
    std::string current_folder_name = "All";
    std::string current_author = "Lewinator56";

    int browse_bank = 0;
    int browse_page = 0;

    static const int BANK_LIST_SLOTS = 14;
    static const int PATCH_LIST_SLOTS = 28;

    int browse_category_patch_count() const {
        if (browse_bank == 0) return (int)patches.size();
        if (browse_bank < 1 || browse_bank >= (int)categories.size()) return 0;
        const std::string& cat = categories[browse_bank];
        int count = 0;
        for (const auto& p : patches) {
            if (p.folder == cat) count++;
        }
        return count;
    }

    int browse_page_count() const {
        int cnt = browse_category_patch_count();
        if (cnt <= 0) return 1;
        return (cnt + PATCH_LIST_SLOTS - 1) / PATCH_LIST_SLOTS;
    }

    int get_patch_index_at_slot(int slot) const {
        if (slot < 0 || slot >= PATCH_LIST_SLOTS) return -1;
        int target = browse_page * PATCH_LIST_SLOTS + slot;
        if (browse_bank == 0) {
            if (target >= 0 && target < (int)patches.size()) return target;
            return -1;
        }
        if (browse_bank < 1 || browse_bank >= (int)categories.size()) return -1;
        const std::string& cat = categories[browse_bank];
        int cur = 0;
        for (size_t i = 0; i < patches.size(); ++i) {
            if (patches[i].folder == cat) {
                if (cur == target) return (int)i;
                cur++;
            }
        }
        return -1;
    }

    HelmInstance() {
        engine.setSampleRate(44100);
        engine.setBufferSize(128);
        controls = engine.getControls();

        // Default polyphony for MPC hardware
        if (controls.count("polyphony")) {
            controls["polyphony"]->set(6.0f);
        }
    }

    ~HelmInstance() {
        clear_all_modulations();
    }

    void clear_all_modulations() {
        for (int i = 0; i < NUM_MOD_SLOTS; ++i) {
            disconnect_slot(i);
        }
    }

    void disconnect_slot(int slot_idx) {
        if (slot_idx < 0 || slot_idx >= NUM_MOD_SLOTS) return;
        ModSlot& slot = mod_slots[slot_idx];
        if (slot.connection) {
            engine.disconnectModulation(slot.connection);
            mod_bank.recycle(slot.connection);
            slot.connection = nullptr;
        }
    }

    void update_slot(int slot_idx) {
        if (slot_idx < 0 || slot_idx >= NUM_MOD_SLOTS) return;
        disconnect_slot(slot_idx);

        ModSlot& slot = mod_slots[slot_idx];
        if (slot.source_idx > 0 && slot.source_idx < NUM_MOD_SOURCES &&
            slot.dest_idx > 0 && slot.dest_idx < NUM_MOD_DESTS &&
            std::fabs(slot.amount) > 1e-4f) {
            const char* src_name = MOD_SOURCES[slot.source_idx];
            const char* dst_name = MOD_DESTS[slot.dest_idx];
            slot.connection = mod_bank.get(src_name, dst_name);
            if (slot.connection) {
                slot.connection->amount.set(slot.amount);
                engine.connectModulation(slot.connection);
            }
        }
    }

    void update_categories() {
        categories.clear();
        categories.push_back("All");
        std::set<std::string> cats;
        for (const auto& pe : patches) {
            if (!pe.folder.empty()) cats.insert(pe.folder);
        }
        for (const auto& c : cats) {
            categories.push_back(c);
        }
        update_filtered_indices();
    }

    void update_filtered_indices() {
        filtered_indices.clear();
        if (current_category <= 0 || current_category >= (int)categories.size()) {
            current_category = 0;
            current_folder_name = "All";
            for (int i = 0; i < (int)patches.size(); ++i) {
                filtered_indices.push_back(i);
            }
        } else {
            const std::string& cat = categories[current_category];
            current_folder_name = cat;
            for (int i = 0; i < (int)patches.size(); ++i) {
                if (patches[i].folder == cat) {
                    filtered_indices.push_back(i);
                }
            }
        }
        if (filtered_indices.empty() && !patches.empty()) {
            filtered_indices.push_back(0);
        }
    }

    void set_category(int cat_idx) {
        if (categories.empty()) return;
        if (cat_idx < 0) cat_idx = 0;
        if (cat_idx >= (int)categories.size()) cat_idx = (int)categories.size() - 1;
        current_category = cat_idx;
        update_filtered_indices();
        load_preset(0);
    }

    void scan_recursive(const std::string& current_path) {
        DIR* d = opendir(current_path.c_str());
        if (!d) return;
        struct dirent* ent;
        std::vector<std::string> subdirs;
        std::vector<std::string> files;
        while ((ent = readdir(d)) != nullptr) {
            if (ent->d_name[0] == '.') continue;
            std::string full = current_path + "/" + ent->d_name;
            DIR* test_sub = opendir(full.c_str());
            if (test_sub) {
                closedir(test_sub);
                subdirs.push_back(full);
            } else {
                files.push_back(full);
            }
        }
        closedir(d);

        std::sort(files.begin(), files.end());
        for (const auto& f : files) {
            if (f.size() > 5 && f.substr(f.size() - 5) == ".helm") {
                PatchEntry pe;
                pe.path = f;
                size_t slash1 = f.find_last_of('/');
                size_t slash2 = (slash1 != std::string::npos && slash1 > 0) ? f.find_last_of('/', slash1 - 1) : std::string::npos;
                if (slash2 != std::string::npos && slash1 != std::string::npos) {
                    pe.folder = f.substr(slash2 + 1, slash1 - slash2 - 1);
                } else {
                    pe.folder = "Factory";
                }
                std::string base = (slash1 != std::string::npos) ? f.substr(slash1 + 1) : f;
                pe.name = base.substr(0, base.size() - 5);
                pe.author = "Lewinator56";

                bool duplicate = false;
                for (const auto& existing : patches) {
                    if (existing.folder == pe.folder && existing.name == pe.name) {
                        duplicate = true;
                        break;
                    }
                }
                if (!duplicate) {
                    patches.push_back(pe);
                }
            }
        }

        std::sort(subdirs.begin(), subdirs.end());
        for (const auto& sd : subdirs) {
            scan_recursive(sd);
        }
    }

    void scan_patches_dir(const std::string& base_dir) {
        patches.clear();
        scan_recursive(base_dir);
        update_categories();
    }

    void load_patch_by_global_index(int patch_idx) {
        if (patch_idx < 0 || patch_idx >= (int)patches.size()) return;
        const PatchEntry& pe = patches[patch_idx];
        std::ifstream ifs(pe.path);
        if (!ifs.is_open()) return;

        std::stringstream ss;
        ss << ifs.rdbuf();
        load_patch_json(ss.str());
        current_patch_name = pe.name;
        current_folder_name = pe.folder;

        for (size_t c = 0; c < categories.size(); ++c) {
            if (categories[c] == pe.folder) {
                current_category = (int)c;
                break;
            }
        }
        update_filtered_indices();
        for (size_t fi = 0; fi < filtered_indices.size(); ++fi) {
            if (filtered_indices[fi] == patch_idx) {
                current_preset = (int)fi;
                break;
            }
        }
    }

    void load_preset(int index) {
        if (filtered_indices.empty()) return;
        if (index < 0) index = 0;
        if (index >= (int)filtered_indices.size()) index = (int)filtered_indices.size() - 1;
        current_preset = index;

        int patch_idx = filtered_indices[index];
        if (patch_idx < 0 || patch_idx >= (int)patches.size()) return;

        const PatchEntry& pe = patches[patch_idx];
        std::ifstream ifs(pe.path);
        if (!ifs.is_open()) return;

        std::stringstream ss;
        ss << ifs.rdbuf();
        load_patch_json(ss.str());
        current_patch_name = pe.name;
        if (current_category > 0 && current_category < (int)categories.size()) {
            current_folder_name = categories[current_category];
        } else {
            current_folder_name = pe.folder;
        }
    }

    void load_patch_json(const std::string& json_str) {
        simple_json::Value root = simple_json::parse(json_str);
        if (root.contains("patch_name")) current_patch_name = root["patch_name"].as_string();
        if (root.contains("folder_name")) current_folder_name = root["folder_name"].as_string();
        if (root.contains("author")) current_author = root["author"].as_string();

        const simple_json::Value& settings = root["settings"];
        if (settings.is_object()) {
            for (const auto& kv : settings.as_object()) {
                if (kv.first == "modulations") continue;
                if (controls.count(kv.first)) {
                    controls[kv.first]->set((float)kv.second.as_float());
                }
            }

            clear_all_modulations();
            const auto& mods = settings["modulations"].as_array();
            int slot_idx = 0;
            for (size_t i = 0; i < mods.size() && slot_idx < NUM_MOD_SLOTS; ++i) {
                std::string src = mods[i]["source"].as_string();
                std::string dst = mods[i]["destination"].as_string();
                float amt = (float)mods[i]["amount"].as_float();

                int s_idx = 0, d_idx = 0;
                for (int s = 1; s < NUM_MOD_SOURCES; ++s) {
                    if (src == MOD_SOURCES[s]) { s_idx = s; break; }
                }
                for (int d = 1; d < NUM_MOD_DESTS; ++d) {
                    if (dst == MOD_DESTS[d]) { d_idx = d; break; }
                }

                if (s_idx > 0 && d_idx > 0) {
                    mod_slots[slot_idx].source_idx = s_idx;
                    mod_slots[slot_idx].dest_idx = d_idx;
                    mod_slots[slot_idx].amount = amt;
                    update_slot(slot_idx);
                    slot_idx++;
                }
            }
        }
    }

    std::string serialize_state() {
        simple_json::Value root;
        root["n"] = current_patch_name;
        root["f"] = current_folder_name;
        root["a"] = current_author;
        root["p"] = current_preset;

        simple_json::Value params_obj;
        for (const auto& kv : controls) {
            params_obj[kv.first] = (double)kv.second->value();
        }
        root["v"] = params_obj;

        simple_json::Value mods_arr;
        for (int i = 0; i < NUM_MOD_SLOTS; ++i) {
            if (mod_slots[i].source_idx > 0 && mod_slots[i].dest_idx > 0 &&
                std::fabs(mod_slots[i].amount) > 1e-4f) {
                simple_json::Value m;
                m["s"] = mod_slots[i].source_idx;
                m["d"] = mod_slots[i].dest_idx;
                m["a"] = (double)mod_slots[i].amount;
                mods_arr.push_back(m);
            }
        }
        root["m"] = mods_arr;
        return root.dump();
    }

    void restore_state(const std::string& json_str) {
        simple_json::Value root = simple_json::parse(json_str);
        if (root.contains("n")) current_patch_name = root["n"].as_string();
        if (root.contains("f")) current_folder_name = root["f"].as_string();
        if (root.contains("a")) current_author = root["a"].as_string();
        if (root.contains("p")) current_preset = root["p"].as_int();

        if (root.contains("v")) {
            for (const auto& kv : root["v"].as_object()) {
                if (controls.count(kv.first)) {
                    controls[kv.first]->set((float)kv.second.as_float());
                }
            }
        }

        clear_all_modulations();
        if (root.contains("m")) {
            const auto& arr = root["m"].as_array();
            for (size_t i = 0; i < arr.size() && (int)i < NUM_MOD_SLOTS; ++i) {
                int s = arr[i]["s"].as_int();
                int d = arr[i]["d"].as_int();
                float a = (float)arr[i]["a"].as_float();
                mod_slots[i].source_idx = s;
                mod_slots[i].dest_idx = d;
                mod_slots[i].amount = a;
                update_slot((int)i);
            }
        }
    }
};

/* ---- mpc_engine_t implementation ---------------------------------------- */

static void *helm_create(const char *data_dir) {
    HelmInstance *inst = new HelmInstance();

    // Look for factory patches in data_dir / relative paths
    std::vector<std::string> search_paths;
    if (data_dir && data_dir[0]) {
        search_paths.push_back(std::string(data_dir));
        search_paths.push_back(std::string(data_dir) + "/patches");
        search_paths.push_back(std::string(data_dir) + "/Factory Presets");
        search_paths.push_back(std::string(data_dir) + "/patches/Factory Presets");
    }
    search_paths.push_back("/sdcard/Synths/Lewinator56 - VST - HelmX/patches");
    search_paths.push_back("/sdcard/Synths/Lewinator56 - VST - HelmX/patches/Factory Presets");
    search_paths.push_back("/sdcard/Synths/Matt Tytel - VST - Helm/patches");
    search_paths.push_back("/sdcard/Synths/Matt Tytel - VST - Helm/patches/Factory Presets");
    search_paths.push_back("./patches");
    search_paths.push_back("./patches/Factory Presets");
    search_paths.push_back("../patches");
    search_paths.push_back("patches");

    for (const auto& path : search_paths) {
        DIR* test_d = opendir(path.c_str());
        if (test_d) {
            closedir(test_d);
            inst->scan_patches_dir(path);
            if (!inst->patches.empty()) {
                inst->load_preset(0);
                break;
            }
        }
    }
    return inst;
}

static void helm_destroy(void *inst) {
    if (inst) delete (HelmInstance *)inst;
}

static void helm_midi(void *inst_ptr, const uint8_t *msg, int len) {
    if (!inst_ptr || len < 1) return;
    HelmInstance *inst = (HelmInstance *)inst_ptr;
    std::lock_guard<std::mutex> lock(inst->mtx);

    uint8_t status = msg[0] & 0xF0;
    uint8_t channel = msg[0] & 0x0F;

    switch (status) {
        case 0x90: { // Note On
            if (len >= 3) {
                uint8_t note = msg[1];
                uint8_t vel = msg[2];
                if (vel > 0) {
                    inst->engine.noteOn((mopo::mopo_float)note, (mopo::mopo_float)vel / 127.0f, 0, channel);
                } else {
                    inst->engine.noteOff((mopo::mopo_float)note);
                }
            }
            break;
        }
        case 0x80: { // Note Off
            if (len >= 2) {
                inst->engine.noteOff((mopo::mopo_float)msg[1]);
            }
            break;
        }
        case 0xB0: { // Control Change
            if (len >= 3) {
                uint8_t cc = msg[1] & 0x7F;
                uint8_t val = msg[2] & 0x7F;
                if (cc == 1) { // Mod Wheel
                    inst->engine.setModWheel((mopo::mopo_float)val / 127.0f, channel);
                } else if (cc == 64) { // Sustain Pedal
                    if (val >= 64) inst->engine.sustainOn();
                    else inst->engine.sustainOff();
                } else if (cc == 120 || cc == 123) { // All Sound / Notes Off
                    inst->engine.allNotesOff();
                }
            }
            break;
        }
        case 0xE0: { // Pitch Bend
            if (len >= 3) {
                int raw = (((int)(msg[2] & 0x7F)) << 7) | (int)(msg[1] & 0x7F);
                float bend = ((float)raw - 8192.0f) / 8192.0f;
                if (bend < -1.0f) bend = -1.0f;
                if (bend > 1.0f) bend = 1.0f;
                inst->engine.setPitchWheel(bend, channel);
            }
            break;
        }
        case 0xD0: { // Channel Aftertouch
            if (len >= 2) {
                inst->engine.setChannelAftertouch(channel, (mopo::mopo_float)msg[1] / 127.0f);
            }
            break;
        }
        case 0xA0: { // Poly Aftertouch
            if (len >= 3) {
                inst->engine.setAftertouch((mopo::mopo_float)msg[1], (mopo::mopo_float)msg[2] / 127.0f);
            }
            break;
        }
        default:
            break;
    }
}

static void helm_set_param(void *inst_ptr, const char *key, const char *val) {
    if (!inst_ptr || !key || !val) return;
    HelmInstance *inst = (HelmInstance *)inst_ptr;
    std::lock_guard<std::mutex> lock(inst->mtx);

    if (strcmp(key, "state") == 0) {
        inst->restore_state(val);
        return;
    }
    if (strcmp(key, "category") == 0) {
        inst->set_category(atoi(val));
        return;
    }
    if (strcmp(key, "category_prev") == 0) {
        inst->set_category(inst->current_category - 1);
        return;
    }
    if (strcmp(key, "category_next") == 0) {
        inst->set_category(inst->current_category + 1);
        return;
    }
    if (strcmp(key, "preset") == 0) {
        int idx = atoi(val);
        inst->load_preset(idx);
        return;
    }
    if (strcmp(key, "preset_prev") == 0) {
        inst->load_preset(inst->current_preset - 1);
        return;
    }
    if (strcmp(key, "preset_next") == 0) {
        inst->load_preset(inst->current_preset + 1);
        return;
    }
    if (strncmp(key, "mod_", 4) == 0) {
        int slot = 0;
        char field[32];
        if (sscanf(key, "mod_%d_%31s", &slot, field) == 2 && slot >= 1 && slot <= NUM_MOD_SLOTS) {
            int slot_idx = slot - 1;
            if (strcmp(field, "source") == 0) {
                inst->mod_slots[slot_idx].source_idx = atoi(val);
                inst->update_slot(slot_idx);
            } else if (strcmp(field, "dest") == 0) {
                int old_dest = inst->mod_slots[slot_idx].dest_idx;
                float old_scale = get_dest_modulation_scale(old_dest);
                float cur_pct = (old_scale > 1e-4f) ? (inst->mod_slots[slot_idx].amount / old_scale) : 0.0f;
                inst->mod_slots[slot_idx].dest_idx = atoi(val);
                float new_scale = get_dest_modulation_scale(inst->mod_slots[slot_idx].dest_idx);
                inst->mod_slots[slot_idx].amount = cur_pct * new_scale;
                inst->update_slot(slot_idx);
            } else if (strcmp(field, "amount") == 0) {
                float val_f = (float)atof(val);
                float pct = val_f / 100.0f;
                float scale = get_dest_modulation_scale(inst->mod_slots[slot_idx].dest_idx);
                inst->mod_slots[slot_idx].amount = pct * scale;
                inst->update_slot(slot_idx);
            }
            return;
        }
    }

    if (strncmp(key, "bank_slot_", 10) == 0) {
        int slot = atoi(key + 10) - 1;
        if (slot >= 0 && slot < HelmInstance::BANK_LIST_SLOTS && slot < (int)inst->categories.size()) {
            inst->browse_bank = slot;
            inst->browse_page = 0;
        }
        return;
    }
    if (strncmp(key, "patch_slot_", 11) == 0) {
        int slot = atoi(key + 11) - 1;
        int idx = inst->get_patch_index_at_slot(slot);
        if (idx >= 0 && idx < (int)inst->patches.size()) {
            inst->load_patch_by_global_index(idx);
        }
        return;
    }
    if (strcmp(key, "patch_page_next") == 0) {
        int pages = inst->browse_page_count();
        inst->browse_page = (inst->browse_page + 1) % pages;
        return;
    }
    if (strcmp(key, "patch_page_prev") == 0) {
        int pages = inst->browse_page_count();
        inst->browse_page = (inst->browse_page - 1 + pages) % pages;
        return;
    }

    if (inst->controls.count(key)) {
        float v = (float)atof(val);
        inst->controls[key]->set(v);
    }
}

static int helm_get_param(void *inst_ptr, const char *key, char *buf, int buf_len) {
    if (!inst_ptr || !key || !buf || buf_len < 1) return 0;
    HelmInstance *inst = (HelmInstance *)inst_ptr;
    std::lock_guard<std::mutex> lock(inst->mtx);

    size_t kl = strlen(key);
    if (kl > 3 && strcmp(key + kl - 3, "_on") == 0) {
        if (strncmp(key, "bank_slot_", 10) == 0) {
            int slot = atoi(key + 10) - 1;
            return snprintf(buf, buf_len, "%d", slot == inst->browse_bank ? 1 : 0);
        }
        if (strncmp(key, "patch_slot_", 11) == 0) {
            int slot = atoi(key + 11) - 1;
            int idx = inst->get_patch_index_at_slot(slot);
            int cur_idx = (!inst->filtered_indices.empty() && inst->current_preset >= 0 && inst->current_preset < (int)inst->filtered_indices.size())
                          ? inst->filtered_indices[inst->current_preset] : -1;
            return snprintf(buf, buf_len, "%d", (idx >= 0 && idx == cur_idx) ? 1 : 0);
        }
    }
    if (strncmp(key, "bank_slot_", 10) == 0) {
        int slot = atoi(key + 10) - 1;
        if (slot < 0 || slot >= HelmInstance::BANK_LIST_SLOTS || slot >= (int)inst->categories.size()) {
            buf[0] = '\0';
            return 0;
        }
        return snprintf(buf, buf_len, "%s", inst->categories[slot].c_str());
    }
    if (strncmp(key, "patch_slot_", 11) == 0) {
        int slot = atoi(key + 11) - 1;
        int idx = inst->get_patch_index_at_slot(slot);
        if (idx < 0 || idx >= (int)inst->patches.size()) {
            buf[0] = '\0';
            return 0;
        }
        return snprintf(buf, buf_len, "%s", inst->patches[idx].name.c_str());
    }
    if (strcmp(key, "browse_bank_name") == 0) {
        if (inst->browse_bank >= 0 && inst->browse_bank < (int)inst->categories.size()) {
            return snprintf(buf, buf_len, "%s", inst->categories[inst->browse_bank].c_str());
        }
        return snprintf(buf, buf_len, "All");
    }
    if (strcmp(key, "patch_page_text") == 0) {
        return snprintf(buf, buf_len, "PAGE %d/%d", inst->browse_page + 1, inst->browse_page_count());
    }

    if (strcmp(key, "state") == 0) {
        std::string s = inst->serialize_state();
        if ((int)s.size() + 1 > buf_len) return 0;
        snprintf(buf, buf_len, "%s", s.c_str());
        return (int)s.size();
    }
    if (strcmp(key, "category") == 0) {
        snprintf(buf, buf_len, "%d", inst->current_category);
        return 1;
    }
    if (strcmp(key, "preset") == 0) {
        snprintf(buf, buf_len, "%d", inst->current_preset);
        return 1;
    }
    if (strcmp(key, "patch_name") == 0) {
        snprintf(buf, buf_len, "%s", inst->current_patch_name.c_str());
        return 1;
    }
    if (strcmp(key, "folder_name") == 0) {
        snprintf(buf, buf_len, "%s", inst->current_folder_name.c_str());
        return 1;
    }
    if (strcmp(key, "author") == 0) {
        snprintf(buf, buf_len, "%s", inst->current_author.c_str());
        return 1;
    }
    if (strncmp(key, "mod_", 4) == 0) {
        int slot = 0;
        char field[32];
        if (sscanf(key, "mod_%d_%31s", &slot, field) == 2 && slot >= 1 && slot <= NUM_MOD_SLOTS) {
            int slot_idx = slot - 1;
            if (strcmp(field, "source") == 0) {
                snprintf(buf, buf_len, "%d", inst->mod_slots[slot_idx].source_idx);
                return 1;
            } else if (strcmp(field, "dest") == 0) {
                snprintf(buf, buf_len, "%d", inst->mod_slots[slot_idx].dest_idx);
                return 1;
            } else if (strcmp(field, "amount") == 0) {
                float scale = get_dest_modulation_scale(inst->mod_slots[slot_idx].dest_idx);
                int val_i = 0;
                if (scale > 1e-4f) {
                    val_i = (int)lroundf((inst->mod_slots[slot_idx].amount / scale) * 100.0f);
                    if (val_i < -100) val_i = -100;
                    if (val_i > 100) val_i = 100;
                }
                snprintf(buf, buf_len, "%d", val_i);
                return 1;
            }
        }
    }

    if (inst->controls.count(key)) {
        float v = inst->controls[key]->value();
        snprintf(buf, buf_len, "%g", v);
        return 1;
    }
    return 0;
}

static void helm_render(void *inst_ptr, int16_t *out_lr, int frames) {
    if (!inst_ptr || !out_lr || frames <= 0) return;
    HelmInstance *inst = (HelmInstance *)inst_ptr;
    std::lock_guard<std::mutex> lock(inst->mtx);

    if (inst->engine.getBufferSize() != frames) {
        inst->engine.setBufferSize(frames);
    }
    inst->engine.process();

    const mopo::mopo_float* l = inst->engine.output(0)->buffer;
    const mopo::mopo_float* r = inst->engine.output(1)->buffer;
    for (int i = 0; i < frames; ++i) {
        out_lr[i * 2]     = float_to_int16(l[i]);
        out_lr[i * 2 + 1] = float_to_int16(r[i]);
    }
}

static const mpc_engine_t HELM_ENGINE = {
    helm_create,
    helm_destroy,
    helm_midi,
    helm_set_param,
    helm_get_param,
    helm_render,
    nullptr /* process is NULL for synths */
};

extern "C" __attribute__((visibility("default"))) const mpc_engine_t *mpc_engine(void) {
    return &HELM_ENGINE;
}
