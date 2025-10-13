#pragma once

#ifndef SAFGUIF_HEADER
#define SAFGUIF_HEADER

#ifdef _MSC_VER
#include <Windows.h>
#endif
#include <string>
#include <vector>

#ifdef _MSC_VER
using std_unicode_string = std::wstring;
#else
using std_unicode_string = std::string;
#endif

inline constexpr const char* window_title = "SAFC\0";
inline std_unicode_string default_reg_path =
#ifdef _MSC_VER
	L"Software\\SAFC\\";
#else
	"";
#endif

inline std::string default_font_name = "Arial";

#define TRY_CATCH(code,msg) try{code}catch(...){std::cout<<msg<<std::endl;}

#ifdef _MSC_VER
#define FORCEDINLINE __forceinline
#else
#define FORCEDINLINE __attribute__((always_inline))
#endif

constexpr float angle_to_radians(float a)
{
	return 0.0174532925f * a;
}

inline float rand_float(float range) 
{
	return ((0 - range) + ((float)rand() / ((float)RAND_MAX / (2 * range))));
}

inline float rand_sign()
{ 
	return ((rand() & 1) ? -1.f : 1.f); 
}

inline float __slowdprog(float a, float b, float progressrate)
{
	return ((a + (progressrate - 1) * b) / progressrate);
}

template<typename OnDestroyFunctor>
class OnDestroyExecutor
{
public:
	OnDestroyExecutor(OnDestroyFunctor&& func) : _f(std::move(func)) {}
	virtual ~OnDestroyExecutor() { _f(); }
private:
	OnDestroyFunctor _f;
};

template<typename OnDestroyFunctor>
OnDestroyExecutor<OnDestroyFunctor> makeOnDestroyExecutor(OnDestroyFunctor&& func)
{
	return OnDestroyExecutor<OnDestroyFunctor>(std::move(func));
}

void ThrowAlert_Error(std::string&& AlertText);
void ThrowAlert_Warning(std::string&& AlertText);
void AddFiles(const std::vector<std_unicode_string>& Filenames);
#pragma warning(disable : 4996)

#define MD_CASE(value) case (value): case ((value|1))
#define MT_CASE(value) MD_CASE(value): MD_CASE((value|2))
#define MO_CASE(value) MT_CASE(value): MT_CASE((value|4))
#define MH_CASE(value) MO_CASE(value): MO_CASE((value|8))

#endif // !SAFGUIF_HU
