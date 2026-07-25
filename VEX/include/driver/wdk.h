#pragma once
#include <Windows.h>
#include <winternl.h>

extern "C" __int64 direct_device_control(
    HANDLE,
    HANDLE,
    PIO_APC_ROUTINE,
    PVOID,
    PIO_STATUS_BLOCK,
    std::uint32_t,
    PVOID,
    std::uint32_t,
    PVOID,
    std::uint32_t
);

enum class e_command_type : std::uint32_t {
	unload_driver,
	is_active,
	get_dtb,
	get_base_address,
	allocate_kernel_memory,
	free_kernel_memory,
	allocate_va_memory,
	free_va_memory,
	rw_phys_memory,
	rw_memory,
	protect_memory,
	protect_memory_pte,
	trampoline,
	expose_kernel_memory,
	mouse_move,
	stream_mode,
	get_kernel_base,
	get_dlls_base,
	get_process_id,
	get_peb
};

struct s_alloc_params {
	std::int32_t process_id;
	std::size_t size;
	std::uint32_t access;
	std::uintptr_t mdl;
	std::uintptr_t address;
};

struct s_read_write_phys_params {
	std::int32_t process_id;
	std::size_t size;
	void* target;
	void* dest;
	std::uint8_t write;
};

struct s_read_write_params {
	std::int32_t process_id;
	std::size_t size;
	void* src;
	void* dst;
	std::uint8_t write;
	void* dirbase;
};

struct s_get_dtb_params {
	std::uintptr_t address;
	std::uintptr_t address2;
};

struct s_get_base_address_params {
	std::int32_t process_id;
	std::uintptr_t base_address;
};

struct s_protect_memory_params {
	std::int32_t process_id;
	std::uintptr_t base_address;
	std::size_t size;
	std::uint32_t protection;
};

struct s_protect_memory_pte_params {
	std::int32_t process_id;
	std::uintptr_t base_address;
	std::size_t size;
	std::uint8_t write;
	std::uint8_t execute;
};

struct s_trampoline_params {
	std::int32_t process_id;
	std::uintptr_t address;
	std::uintptr_t target;
};

struct s_mouse_params {
	long x;
	long y;
	unsigned short button_flags;
};

struct s_stream_mode
{
	uint64_t hwnd;
	uint32_t flag;
};

struct s_kernel_base
{
	void* address;
	char module_name[256];
};

struct s_dll_base
{
	std::int32_t process_id;
	std::uintptr_t address;
	char module_name[256];
};

struct s_get_process_id {
	DWORD process_id;
	char process_name[256];
};


struct s_get_peb_params {
	std::int32_t process_id;
	std::uintptr_t peb_address;
};

struct s_command_data {
	NTSTATUS m_status{};

	union {
		s_get_dtb_params			 dtb;
		s_get_base_address_params	 base_address;
		s_alloc_params				 alloc;
		s_read_write_params		     rw;
		s_read_write_phys_params     rwphys;
		s_protect_memory_params		 protect;
		s_protect_memory_pte_params	 protect_pte;
		s_trampoline_params			 trampoline;
		s_mouse_params               mouse;
		s_stream_mode 		         stream_mode;
		s_kernel_base                kernel_base;
		s_dll_base			         dll_base;
		s_get_process_id             get_process_id;
		s_get_peb_params             get_peb;
	} params;
};

template<typename T>
class c_command {
public:
	using data_type = T;

	c_command() = default;

	c_command(e_command_type type,
		const T& data,
		bool completed = false)
		: m_type(type),
		m_data(data),
		m_completed(completed),
		m_timestamp(0)
	{
	}

	bool is_completed() const noexcept {
		return m_completed;
	}
	void set_completed(bool state) noexcept {
		m_completed = state;
	}

	NTSTATUS get_status() const noexcept {
		return m_data.m_status;
	}
	void set_status(NTSTATUS status_code) noexcept {
		m_data.m_status = status_code;
	}

	e_command_type get_type() const noexcept {
		return m_type;
	}

	const T& get_data() const noexcept {
		return m_data;
	}
	T& get_data()       noexcept {
		return m_data;
	}

private:
	e_command_type    m_type{};       // tipo do comando
	T                 m_data{};       // payload (ex: s_command_data)
	bool              m_completed{ false };
	std::uint64_t     m_timestamp{ 0 };
};