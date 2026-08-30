#include "mono.h"

std::string Mono::managedDirectory;

std::vector<std::pair<std::string, Assembly*>> Mono::assemblyCache;
std::vector<std::pair<std::string, Method*>> Mono::methodCache;
std::vector<std::pair<std::string, Class*>> Mono::klassCache;

void Mono::Attach()
{
	if (!domain)
		domain = mono_get_root_domain();

	if (!domain)
		return;

	mono_thread_attach(domain);
}

void Mono::Initialize()
{
	MH_Initialize();
	_MonoMemory.Initialize();
	API::Initialize();

	Mono::Attach();
}

Class* Object::GetClass()
{
	return (Class*)mono_object_get_class(this);
}

Field* Object::GetField(const char* Name)
{
	return this->GetClass()->GetField(Name);
}

Method* Object::GetMethod(const char* Name, int pCount)
{
	return (Method*)this->GetClass()->GetMethod(Name, pCount);
}

Class* Class::Resolve(
	const char* Asm,
	const char* Namespace,
	const char* Klass)
{
	if (!Asm || !Namespace || !Klass)
		return nullptr;

	if (!*Asm || !*Klass)
		return nullptr;

	std::string key =
		std::string(Asm) + "." +
		std::string(Namespace) + "." +
		std::string(Klass);

	for (auto& entry : Mono::klassCache)
	{
		if (entry.first == key)
			return entry.second;
	}

	Assembly* assembly = Assembly::Resolve(Asm);

	if (!assembly)
		return nullptr;

	MonoImage* image = assembly->GetImage();

	if (!image)
		return nullptr;

	MonoClass* klass = mono_class_from_name(
		image,
		Namespace,
		Klass
	);

	if (!klass)
		return nullptr;

	Class* pKlass =
		reinterpret_cast<Class*>(klass);

	Mono::klassCache.push_back({
		key,
		pKlass
		});

	return pKlass;
}

Field* Class::GetField(const char* Name)
{
	return (Field*)mono_class_get_field_from_name(this, Name);
}

Method* Class::GetMethod(const char* Name, int pCount)
{
	if (!Name)
		return nullptr;

	std::string key =
		std::to_string(reinterpret_cast<uintptr_t>(this)) +
		"." +
		Name +
		"." +
		std::to_string(pCount);

	for (auto& entry : Mono::methodCache)
	{
		if (entry.first == key)
			return entry.second;
	}

	MonoMethod* method =
		mono_class_get_method_from_name(this, Name, pCount);

	if (!method)
		return nullptr;

	Method* pMethod =
		reinterpret_cast<Method*>(method);

	Mono::methodCache.push_back({
		key,
		pMethod
		});

	return pMethod;
}

const char* Class::GetName()
{
	return mono_class_get_name(this);
}

void Class::Methods()
{
	void* iter = nullptr;
	MonoMethod* method;
	while (method = mono_class_get_methods(this, &iter))
	{
		std::cout << mono_method_full_name(method, 1) << "\n";
	}
}

Assembly* Assembly::Resolve(
	const char* Name)
{
	if (!Name || !*Name)
		return nullptr;

	if (!Mono::domain)
		return nullptr;

	std::string assemblyName =
		Mono::NormalizeAssemblyName(Name);

	if (assemblyName.empty())
		return nullptr;

	
	Assembly* cached =
		Mono::FindCachedAssembly(
			assemblyName.c_str()
		);

	if (cached)
		return cached;

	if (Mono::managedDirectory.empty())
	{
		if (!Mono::FindManagedDirectory())
			return nullptr;
	}

	std::string assemblyPath =
		Mono::managedDirectory +
		assemblyName +
		".dll";

	MonoAssembly* monoAssembly =
		mono_domain_assembly_open(
			Mono::domain,
			assemblyPath.c_str()
		);

	if (!monoAssembly)
		return nullptr;

	Assembly* assembly =
		reinterpret_cast<Assembly*>(
			monoAssembly
			);

	Mono::assemblyCache.emplace_back(
		assemblyName,
		assembly
	);

	return assembly;
}

MonoImage* Assembly::GetImage()
{
	return (MonoImage*)mono_assembly_get_image(this);
}

Method* Method::Resolve(
	const char* Asm,
	const char* Namespace,
	const char* Klass,
	const char* Name,
	int pCount)
{
	if (!Asm || !Namespace || !Klass || !Name)
		return nullptr;

	Class* pKlass =
		Class::Resolve(
			Asm,
			Namespace,
			Klass
		);

	if (!pKlass)
		return nullptr;

	return pKlass->GetMethod(
		Name,
		pCount
	);
}

void* Method::GetAddress()
{
	return mono_compile_method(this);
}

void Method::Hook(void* vHk, void* vOrg)
{
	MH_CreateHook(mono_compile_method(this), vHk, (void**)vOrg);
	MH_EnableHook(mono_compile_method(this));
}

Field* Field::Resolve(const char* Asm, const char* Namespace, const char* Klass, const char* Name)
{
	return (Field*)mono_class_get_field_from_name(Class::Resolve(Asm, Namespace, Klass), Name);
}

String* String::New(const char* str)
{
	return (String*)mono_string_new_len(Mono::domain, str, strlen(str));
}

wchar_t* String::c_str()
{
	return (wchar_t*)mono_string_chars(this);
}

Type* Type::Resolve(Class* klass)
{
	return (Type*)mono_class_get_type(klass);
}

Type* Type::Resolve(const char* Asm, const char* Namespace, const char* Klass)
{
	return Type::Resolve(Class::Resolve(Asm, Namespace, Klass));
}

Class* Type::GetClass()
{
	return (Class*)mono_type_get_class(this);
}

Object* Type::GetObjectType()
{
	return (Object*)mono_type_get_object(Mono::domain, this);
}

namespace Mono {
	std::string NormalizeAssemblyName(
		const char* Name)
	{
		if (!Name)
			return {};

		std::string name = Name;

		//
		// Strip directory, if supplied.
		//
		size_t slash =
			name.find_last_of("\\/");

		if (slash != std::string::npos)
			name =
			name.substr(slash + 1);

		//
		// Strip .dll
		//
		if (name.length() >= 4)
		{
			const char* extension =
				name.c_str() +
				name.length() - 4;

			if (_stricmp(
				extension,
				".dll"
			) == 0)
			{
				name.resize(
					name.length() - 4
				);
			}
		}

		return name;
	}

	Assembly* Mono::FindCachedAssembly(
		const char* Name)
	{
		if (!Name || !*Name)
			return nullptr;

		std::string name =
			NormalizeAssemblyName(Name);

		for (auto& entry : assemblyCache)
		{
			if (_stricmp(
				entry.first.c_str(),
				name.c_str()
			) == 0)
			{
				return entry.second;
			}
		}

		return nullptr;
	}

	bool Mono::FindManagedDirectory()
	{
		char modulePath[MAX_PATH]{};

		DWORD length = GetModuleFileNameA(
			nullptr,
			modulePath,
			MAX_PATH
		);

		if (!length)
			return false;

		std::string executablePath(
			modulePath,
			length
		);

		//
		// C:\Games\Game\Game.exe
		// ->
		// C:\Games\Game\
		//
		size_t slash =
			executablePath.find_last_of("\\/");

		if (slash == std::string::npos)
			return false;

		std::string rootDirectory =
			executablePath.substr(
				0,
				slash + 1
			);

		//
		// Get EXE name.
		//
		std::string executableName =
			executablePath.substr(
				slash + 1
			);

		size_t extension =
			executableName.find_last_of('.');

		if (extension != std::string::npos)
			executableName.resize(extension);

		//
		// First try:
		//
		// Game.exe
		// Game_Data\Managed\
		//
		std::string expectedDataDirectory =
			rootDirectory +
			executableName +
			"_Data\\";

		std::string expectedManagedDirectory =
			expectedDataDirectory +
			"Managed\\";

		DWORD attributes =
			GetFileAttributesA(
				expectedManagedDirectory.c_str()
			);

		if (attributes != INVALID_FILE_ATTRIBUTES &&
			(attributes & FILE_ATTRIBUTE_DIRECTORY))
		{
			managedDirectory =
				expectedManagedDirectory;

			return true;
		}

		//
		// Fallback:
		// Search root directory for ANY *_Data folder.
		//
		std::string searchPath =
			rootDirectory + "*";

		WIN32_FIND_DATAA findData{};

		HANDLE hFind =
			FindFirstFileA(
				searchPath.c_str(),
				&findData
			);

		if (hFind == INVALID_HANDLE_VALUE)
			return false;

		do
		{
			if (!(findData.dwFileAttributes &
				FILE_ATTRIBUTE_DIRECTORY))
			{
				continue;
			}

			std::string directoryName =
				findData.cFileName;

			if (directoryName == "." ||
				directoryName == "..")
			{
				continue;
			}

			//
			// Must end with "_Data"
			//
			if (directoryName.length() < 5)
				continue;

			std::string ending =
				directoryName.substr(
					directoryName.length() - 5
				);

			if (_stricmp(
				ending.c_str(),
				"_Data"
			) != 0)
			{
				continue;
			}

			std::string candidate =
				rootDirectory +
				directoryName +
				"\\Managed\\";

			DWORD candidateAttributes =
				GetFileAttributesA(
					candidate.c_str()
				);

			if (candidateAttributes !=
				INVALID_FILE_ATTRIBUTES &&
				(candidateAttributes &
					FILE_ATTRIBUTE_DIRECTORY))
			{
				managedDirectory =
					candidate;

				FindClose(hFind);

				return true;
			}

		} while (
			FindNextFileA(
				hFind,
				&findData
			)
			);

		FindClose(hFind);

		return false;
	}
}