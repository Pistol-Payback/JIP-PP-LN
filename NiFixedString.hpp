#pragma once

const char* __cdecl CreateNiFixedString(const char* inStr);
const char* __cdecl GetNiFixedString(const char* inStr);

class NiCriticalSection {
public:
	CRITICAL_SECTION	m_kCriticalSection;
	UInt32				m_ulThreadOwner;
	UInt32				m_uiLockCount;

	void Lock() {
		EnterCriticalSection(&m_kCriticalSection);
	}

	void Unlock() {
		LeaveCriticalSection(&m_kCriticalSection);
	}
};

class NiMemObject {};


//--------------------------------------------------------------------------------------
// NiGlobalStringTable: Manages pooled, reference‑counted strings.
//--------------------------------------------------------------------------------------
class NiGlobalStringTable : public NiMemObject {
public:
	using Handle = char*;

	// --- Public API: add / remove / query strings in the global pool ---
	static Handle AddString(const char* pcString) {
		return (Handle)GetNiFixedString(pcString);
	}

private:
	NiTArray<Handle>      m_hashArray[512];
	void* unk2000[32];
	NiCriticalSection     m_criticalSection;
	void* unk20A0[24];
};
static_assert(sizeof(NiGlobalStringTable) == 0x2100, "NiGlobalStringTable is the wrong size");

/*
class NiFixedString
{
	using Handle = NiGlobalStringTable::Handle;
	const char* str;

	//UInt32* Meta() const { return (UInt32*)(str - 8); }

	void incrementRef(const char* inStr)
	{
		str = inStr;
		if (str) InterlockedIncrement(&metaPtr()->refCount);
	}

	void decrementRef()
	{
		if (str)
		{
			InterlockedDecrement(&metaPtr()->refCount);
			str = nullptr;
		}
	}

public:

	// Create a new pooled string or fetch existing
	static const char* Create(const char* s) {
		return GetNiFixedString(s);
	}

	NiFixedString() : str(nullptr) {}
	NiFixedString(const char* inStr) : str(Create(inStr)) {}
	NiFixedString(const NiFixedString& inStr) { incrementRef(inStr.str); }
	~NiFixedString() { decrementRef(); }

	const char* Get() const { return str ? str : "NULL"; }

	//operator const char* () const { return str; }

	//-------------------------------------------------------------------------
	// Accessors
	//-------------------------------------------------------------------------
	const char* CStr() const { return str ? str : "NULL"; }
	//operator const char* () const noexcept { return CStr(); }
	explicit operator bool() const noexcept { return str != nullptr; }
	const char* operator*() const noexcept { return str; }

	inline void operator=(const char* inStr)
	{
		decrementRef();
		str = Create(inStr);
	}
	inline void operator=(const NiFixedString& inStr)
	{
		if (str != inStr.str)
			incrementRef(inStr.str);
	}

	//UInt32 RefCount() const { return str ? Meta()[0] : 0; }

	// --- Metadata header (refCount, length) ---
	struct MetaData {
		UInt32 refCount;
		UInt32 length;
	};

	MetaData* metaPtr() const {
		// header is located immediately before the payload pointer
		return reinterpret_cast<MetaData*>(const_cast<char*>(str) - sizeof(MetaData));
	}

	bool isValid() const noexcept { return str != nullptr; }

	//-------------------------------------------------------------------------
	// Comparison
	//-------------------------------------------------------------------------
	bool operator==(const NiFixedString& rhs) const noexcept {
		return str == rhs.str;
	}
	bool operator!=(const NiFixedString& rhs) const noexcept {
		return str != rhs.str;
	}
	friend bool operator<(const NiFixedString& lhs, const NiFixedString& rhs) noexcept {
		return lhs.str < rhs.str;
	}

	//-------------------------------------------------------------------------
	// Metadata queries
	//-------------------------------------------------------------------------
	MetaData getMeta() const noexcept {
		if (!str) return { 0, 0 };
		return *metaPtr();
	}

	UInt32 Length() const noexcept {
		return str ? metaPtr()->length : 0;
	}

	UInt32 RefCount() const noexcept {
		return str ? metaPtr()->refCount : 0;
	}

};
*/

//From Plugins+
class NiFixedString {
public:

	using Handle = NiGlobalStringTable::Handle;

	// --- Metadata header (refCount, length) ---
	struct MetaData {
		UInt32 refCount;
		UInt32 length;
	};

private:
	Handle str{ nullptr };

	// Create a new pooled string or fetch existing
	static Handle Create(const char* s) {
		return NiGlobalStringTable::AddString(s);
	}

	MetaData* metaPtr() const {
		// header is located immediately before the payload pointer
		return reinterpret_cast<MetaData*>(const_cast<char*>(str) - sizeof(MetaData));
	}

	void incrementRef() {
		if (str)
			InterlockedIncrement(&metaPtr()->refCount);
	}
	void decrementRef() {
		if (str)
			InterlockedDecrement(&metaPtr()->refCount);
	}


	void Unset() {
		if (str) {
			// drop one reference
			InterlockedDecrement(&metaPtr()->refCount);
			str = nullptr;
		}
	}

	//For JIP
	void Set(const char* inStr)
	{
		str = const_cast<char*>(inStr);
		if (str) InterlockedIncrement(&metaPtr()->refCount);
	}

public:
	//-------------------------------------------------------------------------
	// Constructors & Destructor
	//-------------------------------------------------------------------------
	NiFixedString() : str(nullptr) {};

	NiFixedString(const char* inStr)
		: str(inStr ? Create(inStr) : nullptr)
	{}

	NiFixedString(const NiFixedString& rhs)
		: str(rhs.str)
	{
		incrementRef();
	}

	NiFixedString(NiFixedString&& rhs) noexcept
		: str(rhs.str)
	{
		rhs.str = nullptr;
	}

	~NiFixedString() {
		Unset();
	}

	//-------------------------------------------------------------------------
	// Accessors
	//-------------------------------------------------------------------------
	const char* CStr() const { return str ? str : "NULL"; }
	//operator const char* () const noexcept { return CStr(); }
	explicit operator bool() const noexcept { return str != nullptr; }
	const char* operator*() const noexcept { return str; }

	bool isValid() const noexcept { return str != nullptr; }

	// Non-owning view
	std::string_view ToStringView() const noexcept {
		if (!str) return {};
		return { str, Length() };
	}

	// Deep copy
	std::string ToString() const {
		if (!str) return {};
		return std::string(str, Length());
	}

	//-------------------------------------------------------------------------
	// Assignment
	//-------------------------------------------------------------------------
	/*
	void Set(const char* inStr) {
		Unset();
		str = inStr ? Create(inStr) : nullptr;
	}


	NiFixedString& operator=(const char* inStr) {
		Unset();
		str = inStr ? Create(inStr) : nullptr;
		return *this;
	}

	NiFixedString& operator=(const NiFixedString& rhs) {
		if (str != rhs.str) {
			decrementRef();
			str = rhs.str;
			incrementRef();
		}
		return *this;
	}
	*/

	//use JIP assignment instead
	inline void operator=(const char* inStr)
	{
		Unset();
		str = Create(inStr);
	}
	inline void operator=(const NiFixedString& inStr)
	{
		if (str != inStr.str)
			Set(inStr.str);
	}

	//Move op
	NiFixedString& operator=(NiFixedString&& rhs) noexcept {
		if (this != &rhs) {
			decrementRef();
			str = rhs.str;
			rhs.str = nullptr;
		}
		return *this;
	}

	//-------------------------------------------------------------------------
	// Comparison
	//-------------------------------------------------------------------------
	bool operator==(const NiFixedString& rhs) const noexcept {
		return str == rhs.str;
	}
	bool operator!=(const NiFixedString& rhs) const noexcept {
		return str != rhs.str;
	}
	friend bool operator<(const NiFixedString& lhs, const NiFixedString& rhs) noexcept {
		return lhs.str < rhs.str;
	}

	//-------------------------------------------------------------------------
	// Metadata queries
	//-------------------------------------------------------------------------
	MetaData getMeta() const noexcept {
		if (!str) return { 0, 0 };
		return *metaPtr();
	}

	UInt32 Length() const noexcept {
		return str ? metaPtr()->length : 0;
	}

	UInt32 refCount() const noexcept {
		return str ? metaPtr()->refCount : 0;
	}

};