#[allow(dead_code)]
// Calculation for the number of Logical CPU cores
// Works on MacOS, BSD, Linux

macro_rules! impl_as {
    (newtype = $ty:ty => $($method:ident : $cast:ty),+ $(,)?) => {
        impl $ty {
            $(
                #[inline(always)]
                pub fn $method(self) -> $cast {
                    self.0 as $cast
                }
            )+
        }
    };
}

pub mod num_cpus {
    // #[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash)]
    // pub struct LogicalCores(usize);

    // impl_as!(newtype = LogicalCores =>
    //     as_u8   : u8,
    //     as_i64  : u64,
    //     as_usize: usize,
    // );

    #[inline]
    pub fn detect() -> usize {
        get_num_cpus()
    }

    #[cfg(any(target_os = "linux"))]
    fn get_num_cpus() -> usize {
        let mut set: libc::cpu_set_t = unsafe { mem::zeroed() };
        if unsafe { libc::sched_getaffinity(0, mem::size_of::<libc::cpu_set_t>(), &mut set) } == 0 {
            let mut count: u32 = 0;
            for i in 0..libc::CPU_SETSIZE as usize {
                if unsafe { libc::CPU_ISSET(i, &set) } {
                    count += 1
                }
            }
            count as usize
        } else {
            let cpus = unsafe { libc::sysconf(libc::_SC_NPROCESSORS_ONLN) };
            if cpus < 1 { 1 } else { cpus as usize }
        }
    }

    #[cfg(any(target_os = "freebsd", target_os = "dragonfly", target_os = "netbsd"))]
    fn get_num_cpus() -> usize {
        use std::ptr;

        let mut cpus: libc::c_uint = 0;
        let mut cpus_size = std::mem::size_of_val(&cpus);

        unsafe {
            cpus = libc::sysconf(libc::_SC_NPROCESSORS_ONLN) as libc::c_uint;
        }
        if cpus < 1 {
            let mut mib = [libc::CTL_HW, libc::HW_NCPU, 0, 0];
            unsafe {
                libc::sysctl(
                    mib.as_mut_ptr(),
                    2,
                    &mut cpus as *mut _ as *mut _,
                    &mut cpus_size as *mut _ as *mut _,
                    ptr::null_mut(),
                    0,
                );
            }
            if cpus < 1 {
                cpus = 1;
            }
        }
        cpus as usize
    }

    #[cfg(target_os = "openbsd")]
    fn get_num_cpus() -> usize {
        use std::ptr;

        let mut cpus: libc::c_uint = 0;
        let mut cpus_size = std::mem::size_of_val(&cpus);
        let mut mib = [libc::CTL_HW, libc::HW_NCPUONLINE, 0, 0];
        let rc: libc::c_int;

        unsafe {
            rc = libc::sysctl(
                mib.as_mut_ptr(),
                2,
                &mut cpus as *mut _ as *mut _,
                &mut cpus_size as *mut _ as *mut _,
                ptr::null_mut(),
                0,
            );
        }
        if rc < 0 {
            cpus = 1;
        }
        cpus as usize
    }

    #[cfg(any(
        target_os = "macos",
        target_os = "ios",
        target_os = "android",
        target_os = "aix",
        target_os = "solaris",
        target_os = "illumos",
        target_os = "fuchsia"
    ))]
    fn get_num_cpus() -> usize {
        // On ARM targets, processors could be turned off to save power.
        // Use `_SC_NPROCESSORS_CONF` to get the real number.
        #[cfg(any(target_arch = "arm", target_arch = "aarch64"))]
        const CONF_NAME: libc::c_int = libc::_SC_NPROCESSORS_CONF;
        #[cfg(not(any(target_arch = "arm", target_arch = "aarch64")))]
        const CONF_NAME: libc::c_int = libc::_SC_NPROCESSORS_ONLN;

        let cpus = unsafe { libc::sysconf(CONF_NAME) };
        if cpus < 1 { 1 } else { cpus as usize }
    }
}
