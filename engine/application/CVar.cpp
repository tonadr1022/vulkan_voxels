#include "CVar.hpp"

#include <algorithm>

#include "imgui.h"
#include "imgui_stdlib.h"

enum class CVarType : char {
  Int,
  Float,
  String,
};

class CVarParameter {
 public:
  friend class CVarSystemImpl;

  uint32_t array_idx;

  CVarType type;
  CVarFlags flags;
  std::string name;
  std::string description;
};

template <typename T>
struct CVarStorage {
  T default_value;
  T current;
  CVarParameter* parameter;
};

template <typename T>
struct CVarArray {
  std::vector<CVarStorage<T>> cvars;
  uint32_t last_cvar{0};
  explicit CVarArray(size_t size) { cvars.reserve(size); }
  T GetCurrent(uint32_t idx) { return cvars[idx].current; }
  T* GetCurrentPtr(uint32_t idx) { return &cvars[idx].current; }
  void SetCurrent(const T& val, uint32_t idx) { cvars[idx].current = val; }

  uint32_t Add(const T& default_value, const T& current_value, CVarParameter* param) {
    uint32_t idx = cvars.size();
    cvars.emplace_back(default_value, current_value, param);
    param->array_idx = idx;
    return idx;
  }
  uint32_t Add(const T& value, CVarParameter* param) {
    uint32_t idx = cvars.size();
    cvars.emplace_back(value, value, param);
    param->array_idx = idx;
    return idx;
  }
};

class CVarSystemImpl : public CVarSystem {
 public:
  CVarParameter* GetCVar(util::string::Hash hash) final {
    auto it = saved_cvars_.find(hash);
    if (it != saved_cvars_.end()) {
      return &it->second;
    }
    return nullptr;
  }
  CVarParameter* CreateFloatCVar(const char* name, const char* description, double default_value,
                                 double current_value) final {
    auto* param = InitCVar(name, description);
    param->type = CVarType::Float;
    GetCVarArray<double>().Add(default_value, current_value, param);
    return param;
  }

  CVarParameter* CreateStringCVar(const char* name, const char* description,
                                  const char* default_value, const char* current_value) final {
    auto* param = InitCVar(name, description);
    param->type = CVarType::Float;
    GetCVarArray<std::string>().Add(default_value, current_value, param);
    return param;
  }
  CVarParameter* CreateIntCVar(const char* name, const char* description, int32_t default_value,
                               int32_t current_value) final {
    auto* param = InitCVar(name, description);
    param->type = CVarType::Int;
    GetCVarArray<int32_t>().Add(default_value, current_value, param);
    return param;
  }

  template <typename T>
  T* GetCVarCurrent(uint32_t hash) {
    CVarParameter* param = GetCVar(hash);
    if (!param) return nullptr;
    return GetCVarArray<T>().GetCurrentPtr(param->array_idx);
  }
  template <typename T>
  void SetCVarCurrent(uint32_t hash, const T& value) {
    CVarParameter* param = GetCVar(hash);
    if (param) {
      GetCVarArray<T>().SetCurrent(value, param->array_idx);
    }
  }

  double* GetFloatCVar(util::string::Hash hash) final { return GetCVarCurrent<double>(hash); }

  void SetFloatCVar(util::string::Hash hash, double value) final {
    SetCVarCurrent<double>(hash, value);
  }

  int32_t* GetIntCVar(util::string::Hash hash) final { return GetCVarCurrent<int32_t>(hash); }
  void SetIntCVar(util::string::Hash hash, int32_t value) final {
    SetCVarCurrent<int32_t>(hash, value);
  }
  const char* GetStringCVar(util::string::Hash hash) final {
    return GetCVarCurrent<std::string>(hash)->c_str();
  }
  void SetStringCVar(util::string::Hash hash, const char* value) final {
    SetCVarCurrent<std::string>(hash, value);
  }

  template <typename T>
  CVarArray<T>& GetCVarArray();

  template <>
  CVarArray<int32_t>& GetCVarArray() {
    return int_cvars_;
  }

  template <>
  CVarArray<double>& GetCVarArray() {
    return float_cvars_;
  }

  template <>
  CVarArray<std::string>& GetCVarArray() {
    return string_cvars_;
  }

  static CVarSystemImpl& Get() { return static_cast<CVarSystemImpl&>(CVarSystem::Get()); }

  void ImGuiLabel(const char* label, float text_width) {
    constexpr const float LeftPad = 50;
    constexpr const float EditorWidth = 100;
    const ImVec2 line_start = ImGui::GetCursorScreenPos();
    float full_width = text_width + LeftPad;
    ImGui::Text("%s", label);
    ImGui::SameLine();
    ImGui::SetCursorScreenPos(ImVec2{line_start.x + full_width, line_start.y});
    ImGui::SetNextItemWidth(EditorWidth);
  }

  void DrawImGuiEditCVarParameter(CVarParameter* p, float text_width) {
    const bool is_read_only =
        static_cast<uint32_t>(p->flags) & static_cast<uint32_t>(CVarFlags::EditReadOnly);
    const bool is_checkbox =
        static_cast<uint32_t>(p->flags) & static_cast<uint32_t>(CVarFlags::EditCheckbox);
    const bool is_drag =
        static_cast<uint32_t>(p->flags) & static_cast<uint32_t>(CVarFlags::EditFloatDrag);

    switch (p->type) {
      case CVarType::Int:
        if (is_read_only) {
          std::string display_format = p->name + "= %i";
          ImGui::Text(display_format.c_str(), GetCVarArray<int32_t>().GetCurrent(p->array_idx));
        } else {
          if (is_checkbox) {
            ImGuiLabel(p->name.c_str(), text_width);
            ImGui::PushID(p);
            bool is_checked = GetCVarArray<int32_t>().GetCurrent(p->array_idx) != 0;
            if (ImGui::Checkbox("", &is_checked)) {
              GetCVarArray<int32_t>().SetCurrent(static_cast<int32_t>(is_checked), p->array_idx);
            }
            ImGui::PopID();
          } else {
            ImGuiLabel(p->name.c_str(), text_width);
            ImGui::PushID(p);
            ImGui::InputInt("", GetCVarArray<int32_t>().GetCurrentPtr(p->array_idx));
            ImGui::PopID();
          }
        }
        break;
      case CVarType::Float:
        if (is_read_only) {
          std::string display_format = p->name + "= %f";
          ImGui::Text(display_format.c_str(), GetCVarArray<int32_t>().GetCurrent(p->array_idx));
        } else {
          ImGuiLabel(p->name.c_str(), text_width);
          ImGui::PushID(p);
          if (is_drag) {
            float d = GetCVarArray<double>().GetCurrent(p->array_idx);
            if (ImGui::DragFloat("", &d)) {
              *GetCVarArray<double>().GetCurrentPtr(p->array_idx) = static_cast<double>(d);
            }
          } else {
            ImGui::InputDouble("", GetCVarArray<double>().GetCurrentPtr(p->array_idx));
          }
          ImGui::PopID();
        }
        break;
      case CVarType::String:
        if (is_read_only) {
          std::string display_format = p->name + "= %s";
          ImGui::Text(display_format.c_str(),
                      GetCVarArray<std::string>().GetCurrent(p->array_idx).c_str());
        } else {
          ImGuiLabel(p->name.c_str(), text_width);
          ImGui::PushID(p);
          ImGui::InputText("", GetCVarArray<std::string>().GetCurrentPtr(p->array_idx));
          ImGui::PopID();
        }
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("%s", p->description.c_str());
    }
  }

  bool show_advanced = true;
  std::string search_txt;
  std::unordered_map<std::string, std::vector<CVarParameter*>> categorized_params;
  void DrawImGuiEditor() final {
    ImGui::InputText("Filter", &search_txt);
    ImGui::Checkbox("Advanced", &show_advanced);
    active_edit_parameters_.clear();
    auto add_to_edit_list = [&](CVarParameter* param) {
      bool hidden = static_cast<uint32_t>(param->flags) & static_cast<uint32_t>(CVarFlags::NoEdit);
      bool advanced =
          static_cast<uint32_t>(param->flags) & static_cast<uint32_t>(CVarFlags::Advanced);
      if (!hidden && (show_advanced || !advanced) &&
          param->name.find(search_txt) != std::string::npos) {
        active_edit_parameters_.emplace_back(param);
      }
    };

    for (auto& v : GetCVarArray<int32_t>().cvars) {
      add_to_edit_list(v.parameter);
    }
    for (auto& v : GetCVarArray<double>().cvars) {
      add_to_edit_list(v.parameter);
    }
    for (auto& v : GetCVarArray<std::string>().cvars) {
      add_to_edit_list(v.parameter);
    }
    auto edit_params = [this](std::span<CVarParameter*> params) {
      std::ranges::sort(params,
                        [](CVarParameter* a, CVarParameter* b) { return a->name < b->name; });
      float max_text_width = 0;
      for (CVarParameter* p : params) {
        max_text_width = std::max(max_text_width, ImGui::CalcTextSize(p->name.c_str()).x);
      }
      for (CVarParameter* p : params) {
        DrawImGuiEditCVarParameter(p, max_text_width);
      }
    };
    // categorize by "."
    if (active_edit_parameters_.size() > 10) {
      categorized_params.clear();
      for (CVarParameter* p : active_edit_parameters_) {
        size_t dot_pos = p->name.find_first_of('.');
        std::string category;
        if (dot_pos != std::string::npos) {
          category = p->name.substr(0, dot_pos);
        }
        categorized_params[category].emplace_back(p);
      }
      for (auto& [category, params] : categorized_params) {
        if (ImGui::BeginMenu(category.c_str())) {
          edit_params(params);
        }
      }
    } else {
      edit_params(active_edit_parameters_);
    }
  }

 private:
  CVarParameter* InitCVar(const char* name, const char* description) {
    if (GetCVar(name)) return nullptr;
    uint32_t name_hash = util::string::Hash{name};
    auto r = saved_cvars_.emplace(name_hash, CVarParameter{});
    CVarParameter* new_param = &r.first->second;
    new_param->name = name;
    new_param->description = description;
    return new_param;
  }

  std::vector<CVarParameter*> active_edit_parameters_;
  std::unordered_map<uint32_t, CVarParameter> saved_cvars_;
  CVarArray<int32_t> int_cvars_{200};
  CVarArray<double> float_cvars_{200};
  CVarArray<std::string> string_cvars_{200};
};

CVarSystem& CVarSystem::Get() {
  static CVarSystemImpl impl{};
  return impl;
}
AutoCVarFloat::AutoCVarFloat(const char* name, const char* description, double default_value,
                             CVarFlags flags) {
  CVarParameter* param =
      CVarSystemImpl::Get().CreateFloatCVar(name, description, default_value, default_value);
  param->flags = flags;
  idx_ = param->array_idx;
}

namespace {

template <typename T>
T GetCVarCurrentByIndex(uint32_t idx) {
  return CVarSystemImpl::Get().GetCVarArray<T>().GetCurrent(idx);
}
template <typename T>
T* GetCVarCurrentPtrByIndex(uint32_t idx) {
  return CVarSystemImpl::Get().GetCVarArray<T>().GetCurrentPtr(idx);
}

template <typename T>
void SetCVarByIdx(uint32_t idx, const T& data) {
  CVarSystemImpl::Get().GetCVarArray<T>().SetCurrent(data, idx);
}

}  // namespace

double AutoCVarFloat::Get() { return GetCVarCurrentByIndex<CVarType>(idx_); }

double* AutoCVarFloat::GetPtr() { return GetCVarCurrentPtrByIndex<CVarType>(idx_); }

float AutoCVarFloat::GetFloat() { return static_cast<float>(Get()); }

float* AutoCVarFloat::GetFloatPtr() { return reinterpret_cast<float*>(GetPtr()); }

void AutoCVarFloat::Set(double val) { SetCVarByIdx(idx_, val); }

AutoCVarInt::AutoCVarInt(const char* name, const char* desc, int initial_value, CVarFlags flags) {
  CVarParameter* param =
      CVarSystemImpl::Get().CreateIntCVar(name, desc, initial_value, initial_value);
  param->flags = flags;
  idx_ = param->array_idx;
}

int32_t AutoCVarInt::Get() { return GetCVarCurrentByIndex<CVarType>(idx_); }

int32_t* AutoCVarInt::GetPtr() { return GetCVarCurrentPtrByIndex<CVarType>(idx_); }

void AutoCVarInt::Set(int32_t val) { SetCVarByIdx(idx_, val); }
AutoCVarString::AutoCVarString(const char* name, const char* description, const char* default_value,
                               CVarFlags flags) {
  CVarParameter* param =
      CVarSystemImpl::Get().CreateStringCVar(name, description, default_value, default_value);
  param->flags = flags;
  idx_ = param->array_idx;
}

const char* AutoCVarString::Get() { return GetCVarCurrentPtrByIndex<CVarType>(idx_)->c_str(); }

void AutoCVarString::Set(std::string&& val) { SetCVarByIdx<CVarType>(idx_, val); }
