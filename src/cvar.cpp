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
	int32_t lastCVar{ 0 }
};

uint32_t Hash(const char* str)
{
	return StringUtils::fnv1a_32(str, strlen(str));
}

class CVarSystemImpl : public CVarSystem
{
public:
	virtual CVarParameter* GetCVar(StringUtils::StringHash hash) override final
	{
		throw std::logic_error("The method or operation is not implemented.");
	}


	virtual double* GetFloatCVar(StringUtils::StringHash hash) override final
	{
		throw std::logic_error("The method or operation is not implemented.");
	}


	virtual int32_t* GetIntCVar(StringUtils::StringHash hash) override final
	{
		throw std::logic_error("The method or operation is not implemented.");
	}


	virtual const char* GetStringCVar(StringUtils::StringHash hash) override final
	{
		throw std::logic_error("The method or operation is not implemented.");
	}


	virtual void SetFloatCVar(StringUtils::StringHash hash, double value) override final
	{
		throw std::logic_error("The method or operation is not implemented.");
	}


	virtual void SetIntCVar(StringUtils::StringHash hash, int32_t value) override final
	{
		throw std::logic_error("The method or operation is not implemented.");
	}


	virtual void SetStringCVar(StringUtils::StringHash hash, const char* value) override final
	{
		throw std::logic_error("The method or operation is not implemented.");
	}


	virtual CVarParameter* CreateFloatCVar(const char* name, const char* description, double defaultValue, double currentValue) override final
	{
		throw std::logic_error("The method or operation is not implemented.");
	}


	virtual CVarParameter* CreateIntCVar(const char* name, const char* description, int32_t defaultValue, int32_t currentValue) override final
	{
		throw std::logic_error("The method or operation is not implemented.");
	}


	virtual CVarParameter* CreateStringCVar(const char* name, const char* description, const char* defaultValue, const char* currentValue) override final
	{
		throw std::logic_error("The method or operation is not implemented.");
	}


	virtual void DrawImguiEditor() override final
	{
		throw std::logic_error("The method or operation is not implemented.");
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
};