#include <cvar.h>

#include <unordered_map>
#include <array>
#include <algorithm>
#include <imgui.h>
#include <imgui_stdlib.h>
#include <imgui_internal.h>

#include <shared_mutex>

enum class CVarType : char
{
	Int,
	Float,
	String,
};

class  CVarParameter
{
public:
	friend class CVarSystemImpl;

	int32_t arrayIndex;

	CVarType type;
	CVarFlags flags;
	std::string name;
	std::string description;
};

template<typename T>
struct CVarStorage
{
	T initial;
	T current;
	CVarParameter* parameter;
};

template<typename T>
struct CVarArray
{
	CVarArray(size_t size)
	{
		cvars = new CVarStorage<T>[size]();
	}

	CVarStorage<T>* GetCurrentStorage(int32_t index)
	{
		return &cvars[index];
	}

	T* GetCurrentPtr(int32_t index)
	{
		return &cvars[index].current;
	};

	T GetCurrent(int32_t index)
	{
		return cvars[index].current;
	};

	void SetCurrent(const T& val, int32_t index)
	{
		cvars[index].current = val;
	}

	

	int Add(const T& initialValue, const T& currentValue, CVarParameter* param)
	{
		int index = lastCVar;

		cvars[index].current = currentValue;
		cvars[index].initial = initialValue;
		cvars[index].parameter = param;

		param->arrayIndex = index;
		lastCVar++;
		return index;
	}

	int Add(const T& value, CVarParameter* param)
	{
		return Add(value, value, param);
	}

	CVarStorage<T>* cvars{ nullptr };
	int32_t lastCVar{ 0 };
};

uint32_t Hash(const char* str)
{
	return StringUtils::fnv1a_32(str, strlen(str));
}

void Label(const char* label, float textWidth)
{
	constexpr float Slack = 50;
	constexpr float EditorWidth = 100;

	ImGuiWindow* window = ImGui::GetCurrentWindow();
	const ImVec2 lineStart = ImGui::GetCursorScreenPos();
	const ImGuiStyle& style = ImGui::GetStyle();
	float fullWidth = textWidth + Slack;

	ImVec2 textSize = ImGui::CalcTextSize(label);

	ImVec2 startPos = ImGui::GetCursorScreenPos();

	ImGui::Text(label);

	ImVec2 finalPos = { startPos.x + fullWidth, startPos.y };

	ImGui::SameLine();
	ImGui::SetCursorScreenPos(finalPos);

	ImGui::SetNextItemWidth(EditorWidth);
}

class CVarSystemImpl : public CVarSystem
{
public:
	virtual CVarParameter* GetCVar(StringUtils::StringHash hash) override final
	{
		std::shared_lock lock(m_Mutex);

		auto it = m_SavedCVars.find(hash);

		if (it != m_SavedCVars.end())
			return &((*it).second);

		return nullptr;
	}


	virtual double* GetFloatCVar(StringUtils::StringHash hash) override final
	{
		return GetCVarCurrent<double>(hash);
	}


	virtual int32_t* GetIntCVar(StringUtils::StringHash hash) override final
	{
		return GetCVarCurrent<int32_t>(hash);
	}


	virtual const char* GetStringCVar(StringUtils::StringHash hash) override final
	{
		return GetCVarCurrent<std::string>(hash)->c_str();
	}


	virtual void SetFloatCVar(StringUtils::StringHash hash, double value) override final
	{
		SetCVarCurrent<double>(hash, value);
	}


	virtual void SetIntCVar(StringUtils::StringHash hash, int32_t value) override final
	{
		SetCVarCurrent<int32_t>(hash, value);
	}


	virtual void SetStringCVar(StringUtils::StringHash hash, const char* value) override final
	{
		SetCVarCurrent<std::string>(hash, value);
	}


	virtual CVarParameter* CreateFloatCVar(const char* name, const char* description, double defaultValue, double currentValue) override final
	{
		std::unique_lock lock(m_Mutex);
		CVarParameter* param = InitCVar(name, description);
		if (!param)
			return nullptr;

		param->type = CVarType::Float;
		GetCVarArray<double>()->Add(defaultValue, currentValue, param);
		return param;
	}


	virtual CVarParameter* CreateIntCVar(const char* name, const char* description, int32_t defaultValue, int32_t currentValue) override final
	{
		std::unique_lock lock(m_Mutex);
		CVarParameter* param = InitCVar(name, description);
		if (!param)
			return nullptr;

		param->type = CVarType::Int;
		GetCVarArray<int32_t>()->Add(defaultValue, currentValue, param);
		return param;
	}


	virtual CVarParameter* CreateStringCVar(const char* name, const char* description, const char* defaultValue, const char* currentValue) override final
	{
		std::unique_lock lock(m_Mutex);
		CVarParameter* param = InitCVar(name, description);
		if (!param)
			return nullptr;

		param->type = CVarType::String;
		GetCVarArray<std::string>()->Add(defaultValue, currentValue, param);
		return param;
	}

	void EditParameter(CVarParameter* p, float textWidth)
	{
		const bool readonlyFlag = ((uint32_t)p->flags & (uint32_t)CVarFlags::EditReadOnly);
		const bool checkboxFlag = ((uint32_t)p->flags & (uint32_t)CVarFlags::EditCheckbox);
		const bool dragFlag = ((uint32_t)p->flags & (uint32_t)CVarFlags::EditFloatDrag);

		switch (p->type)
		{
		case CVarType::Int:
			if (readonlyFlag)
			{
				std::string displayFormat = p->name + "=%i";
				ImGui::Text(displayFormat.c_str(), GetCVarArray<int32_t>()->GetCurrent(p->arrayIndex));
			}
			else
			{
				if (checkboxFlag)
				{
					bool bCheckBox = GetCVarArray<int32_t>()->GetCurrent(p->arrayIndex) != 0;
					Label(p->name.c_str(), textWidth);

					ImGui::PushID(p->name.c_str());

					if (ImGui::Checkbox("", &bCheckBox))
					{
						GetCVarArray<int32_t>()->SetCurrent(bCheckBox ? 1 : 0 , p->arrayIndex);
					}
					ImGui::PopID();
				}
				else
				{
					Label(p->name.c_str(), textWidth);
					ImGui::PushID(p->name.c_str());
					ImGui::InputInt("", GetCVarArray<int32_t>()->GetCurrentPtr(p->arrayIndex));
					ImGui::PopID();
				}
			}
			break;
		case CVarType::Float:
			if (readonlyFlag)
			{
				std::string displayFormat = p->name + "= %f";
				ImGui::Text(displayFormat.c_str(), GetCVarArray<double>()->GetCurrent(p->arrayIndex));
			}
			else
			{
				Label(p->name.c_str(), textWidth);
				ImGui::PushID(p->name.c_str());
				if (dragFlag)
				{
					ImGui::InputDouble("", GetCVarArray<double>()->GetCurrentPtr(p->arrayIndex), 0, 0, "%.3f");
				}
				else
				{
					ImGui::InputDouble("", GetCVarArray<double>()->GetCurrentPtr(p->arrayIndex), 0, 0, "%.3f");
				}
				ImGui::PopID();
			}
			break;
		case CVarType::String:

			if (readonlyFlag)
			{
				std::string displayFormat = p->name + "= %s";
				ImGui::PushID(p->name.c_str());
				ImGui::Text(displayFormat.c_str(), GetCVarArray<std::string>()->GetCurrent(p->arrayIndex));

				ImGui::PopID();
			}
			else
			{
				Label(p->name.c_str(), textWidth);
				ImGui::InputText("", GetCVarArray<std::string>()->GetCurrentPtr(p->arrayIndex));

				ImGui::PopID();
			}
			break;
		default:
			break;
		}
	}
	virtual void DrawImguiEditor() override final
	{
		static std::string searchText = "";

		ImGui::InputText("Filter", &searchText);
		static bool bShowAdvanced = false;
		ImGui::Checkbox("Advanced", &bShowAdvanced);
		ImGui::Separator();
		m_CachedEditParameters.clear();

		auto addToEditList = [&](auto parameter)
		{
			bool bHidden = ((uint32_t)parameter->flags & (uint32_t)CVarFlags::Noedit);
			bool bIsAdvanced = ((uint32_t)parameter->flags & (uint32_t)CVarFlags::Advanced);

			if (!bHidden)
			{
				if (!(!bShowAdvanced && bIsAdvanced) && parameter->name.find(searchText) != std::string::npos)
				{
					m_CachedEditParameters.push_back(parameter);
				};
			}
		};

		for (int i = 0; i < GetCVarArray<int32_t>()->lastCVar; i++)
		{
			addToEditList(GetCVarArray<int32_t>()->cvars[i].parameter);
		}
		for (int i = 0; i < GetCVarArray<double>()->lastCVar; i++)
		{
			addToEditList(GetCVarArray<double>()->cvars[i].parameter);
		}
		for (int i = 0; i < GetCVarArray<std::string>()->lastCVar; i++)
		{
			addToEditList(GetCVarArray<std::string>()->cvars[i].parameter);
		}

		if (m_CachedEditParameters.size() > 10)
		{
			std::unordered_map<std::string, std::vector<CVarParameter*>> categorizedParams;

			//insert all the edit parameters into the hashmap by category
			for (auto p : m_CachedEditParameters)
			{
				int dotPos = -1;
				//find where the first dot is to categorize
				for (int i = 0; i < p->name.length(); i++)
				{
					if (p->name[i] == '.')
					{
						dotPos = i;
						break;
					}
				}
				std::string category = "";
				if (dotPos != -1)
				{
					category = p->name.substr(0, dotPos);
				}

				auto it = categorizedParams.find(category);
				if (it == categorizedParams.end())
				{
					categorizedParams[category] = std::vector<CVarParameter*>();
					it = categorizedParams.find(category);
				}
				it->second.push_back(p);
			}

			for (auto [category, parameters] : categorizedParams)
			{
				//alphabetical sort
				std::sort(parameters.begin(), parameters.end(), [](CVarParameter* A, CVarParameter* B)
					{
						return A->name < B->name;
					});

				if (ImGui::BeginMenu(category.c_str()))
				{
					float maxTextWidth = 0;

					for (auto p : parameters)
					{
						maxTextWidth = std::max(maxTextWidth, ImGui::CalcTextSize(p->name.c_str()).x);
					}
					for (auto p : parameters)
					{
						EditParameter(p, maxTextWidth);
					}

					ImGui::EndMenu();
				}
			}
		}
		else
		{
			//alphabetical sort
			std::sort(m_CachedEditParameters.begin(), m_CachedEditParameters.end(), [](CVarParameter* A, CVarParameter* B)
				{
					return A->name < B->name;
				});
			float maxTextWidth = 0;
			for (auto p : m_CachedEditParameters)
			{
				maxTextWidth = std::max(maxTextWidth, ImGui::CalcTextSize(p->name.c_str()).x);
			}
			for (auto p : m_CachedEditParameters)
			{
				EditParameter(p, maxTextWidth);
			}
		}
	}

	constexpr static int MAX_INT_CVARS = 1000;
	CVarArray<int32_t> intCVars{ MAX_INT_CVARS };

	constexpr static int MAX_FLOAT_CVARS = 1000;
	CVarArray<double> floatCVars{MAX_FLOAT_CVARS};

	constexpr static int MAX_STRING_CVARS = 200;
	CVarArray<std::string> stringCVars{MAX_STRING_CVARS};

	template<typename T>
	CVarArray<T>* GetCVarArray();

	template<>
	CVarArray<int32_t>* GetCVarArray()
	{
		return &intCVars;
	}

	template<>
	CVarArray<double>* GetCVarArray()
	{
		return &floatCVars;
	}

	template<>
	CVarArray<std::string>* GetCVarArray()
	{
		return &stringCVars;
	}

	template<typename T>
	T* GetCVarCurrent(uint32_t namehash) {
		CVarParameter* par = GetCVar(namehash);
		if (!par)
		{
			return nullptr;
		}
		else
		{
			return GetCVarArray<T>()->GetCurrentPtr(par->arrayIndex);
		}
	}

	template<typename T>
	void SetCVarCurrent(uint32_t namehash, const T& value)
	{
		CVarParameter* cvar = GetCVar(namehash);
		if (cvar)
		{
			GetCVarArray<T>()->SetCurrent(value, cvar->arrayIndex);
		}
	}

	static CVarSystemImpl* Get()
	{
		return static_cast<CVarSystemImpl*>(CVarSystem::Get());
	}

private:
	std::shared_mutex m_Mutex;
	
	CVarParameter* InitCVar(const char* name, const char* description);

	std::unordered_map<uint32_t, CVarParameter> m_SavedCVars;

	std::vector<CVarParameter*> m_CachedEditParameters;
};

CVarParameter* CVarSystemImpl::InitCVar(const char* name, const char* description)
{
	uint32_t namehash = StringUtils::StringHash{ name };
	m_SavedCVars.insert(std::pair(namehash, CVarParameter{}));

	CVarParameter& newParam = m_SavedCVars[namehash];

	newParam.name = name;
	newParam.description = description;

	return &newParam;
}

CVarSystem* CVarSystem::Get()
{
	static CVarSystemImpl cvarSys{};
	return &cvarSys;
}

template<typename T>
T GetCurrentCVarByIndex(int32_t index)
{
	return CVarSystemImpl::Get()->GetCVarArray<T>()->GetCurrent(index);
}

template<typename T>
T* GetCurrentCVarPtrByIndex(int32_t index)
{
	return CVarSystemImpl::Get()->GetCVarArray<T>()->GetCurrentPtr(index);
}

template<typename T>
void SetCurrentCVarByIndex(int32_t index, const T& value)
{
	CVarSystemImpl::Get()->GetCVarArray<T>()->SetCurrent(value, index);
}

AutoCVar_Float::AutoCVar_Float(const char* name, const char* description, double defaultValue, CVarFlags flags /*= CVarFlags::None*/)
{
	CVarParameter* cvar = CVarSystem::Get()->CreateFloatCVar(name, description, defaultValue, defaultValue);
	cvar->flags = flags;
	index = cvar->arrayIndex;
}


double AutoCVar_Float::Get()
{
	return GetCurrentCVarByIndex<CVarType>(index);
}

double* AutoCVar_Float::GetPtr()
{
	return GetCurrentCVarPtrByIndex<CVarType>(index);
}

float AutoCVar_Float::GetFloat()
{
	return static_cast<float>(Get());
}

float* AutoCVar_Float::GetFloatPtr()
{
	float* result = reinterpret_cast<float*>(GetPtr());
	return result;
}

void AutoCVar_Float::Set(double val)
{
	SetCurrentCVarByIndex<CVarType>(index, val);
}

AutoCVar_Int::AutoCVar_Int(const char* name, const char* description, int32_t defaultValue, CVarFlags flags /*= CVarFlags::None*/)
{
	CVarParameter* cvar = CVarSystem::Get()->CreateIntCVar(name, description, defaultValue, defaultValue);
	cvar->flags = flags;
	index = cvar->arrayIndex;
}

int32_t AutoCVar_Int::Get()
{
	return GetCurrentCVarByIndex<CVarType>(index);
}

int32_t* AutoCVar_Int::GetPtr()
{
	return GetCurrentCVarPtrByIndex<CVarType>(index);
}

void AutoCVar_Int::Set(int32_t val)
{
	SetCurrentCVarByIndex<CVarType>(index, val);
}

void AutoCVar_Int::Toggle()
{
	bool enabled = Get() != 0;
	Set(enabled ? 0 : 1);
}

AutoCVar_String::AutoCVar_String(const char* name, const char* description, const char* defaultValue, CVarFlags flags /*= CVarFlags::None*/)
{
	CVarParameter* cvar = CVarSystem::Get()->CreateStringCVar(name, description, defaultValue, defaultValue);
	cvar->flags = flags;
	index = cvar->arrayIndex;
}

const char* AutoCVar_String::Get()
{
	return GetCurrentCVarByIndex<CVarType>(index).c_str();
}

void AutoCVar_String::Set(std::string&& val)
{
	SetCurrentCVarByIndex<CVarType>(index, val);

}
