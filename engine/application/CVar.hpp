#pragma once

// inspired by https://github.com/vblanco20-1/vulkan-guide/blob/engine/extra-engine/cvars.h

#include "application/StringUtil.hpp"

enum class CVarFlags : uint16_t {
  None = 0,
  NoEdit = 1 << 1,
  EditReadOnly = 1 << 2,
  Advanced = 1 << 3,

  EditCheckbox = 1 << 8,
  EditFloatDrag = 1 << 9,
};

class CVarParameter;
class CVarSystem {
 public:
  static CVarSystem& Get();
  virtual CVarParameter* GetCVar(util::string::Hash hash) = 0;
  virtual CVarParameter* CreateFloatCVar(const char* name, const char* description,
                                         double default_value, double current_value) = 0;
  virtual CVarParameter* CreateIntCVar(const char* name, const char* description,
                                       int32_t default_value, int32_t current_value) = 0;
  virtual CVarParameter* CreateStringCVar(const char* name, const char* description,
                                          const char* default_value, const char* current_value) = 0;
  virtual double* GetFloatCVar(util::string::Hash hash) = 0;
  virtual void SetFloatCVar(util::string::Hash hash, double value) = 0;
  virtual int32_t* GetIntCVar(util::string::Hash hash) = 0;
  virtual void SetIntCVar(util::string::Hash hash, int32_t value) = 0;
  virtual const char* GetStringCVar(util::string::Hash hash) = 0;
  virtual void SetStringCVar(util::string::Hash hash, const char* value) = 0;
  virtual void DrawImGuiEditor() = 0;
};

template <typename T>
struct AutoCVar {
 protected:
  uint32_t idx_;
  using CVarType = T;
};

struct AutoCVarInt : AutoCVar<int32_t> {
  AutoCVarInt(const char* name, const char* desc, int initial_value, CVarFlags flags);
  int32_t Get();
  int32_t* GetPtr();
  void Set(int32_t val);
};

struct AutoCVarFloat : AutoCVar<double> {
  AutoCVarFloat(const char* name, const char* description, double default_value,
                CVarFlags flags = CVarFlags::None);
  double Get();
  double* GetPtr();
  float GetFloat();
  float* GetFloatPtr();
  void Set(double val);
};

struct AutoCVarString : AutoCVar<std::string> {
  AutoCVarString(const char* name, const char* description, const char* default_value,
                 CVarFlags flags = CVarFlags::None);
  const char* Get();
  void Set(std::string&& val);
};
