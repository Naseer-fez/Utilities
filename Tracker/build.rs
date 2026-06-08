use std::env;
use std::process::Command;
use std::path::Path;

fn main() {
    let target_os = env::var("CARGO_CFG_TARGET_OS").unwrap();
    if target_os == "windows" {
        let out_dir = env::var("OUT_DIR").unwrap();
        let res_path = Path::new(&out_dir).join("app.res");
        
        let status = Command::new("windres")
            .args(["app.rc", "-O", "coff", "-o"])
            .arg(&res_path)
            .status()
            .expect("Failed to execute windres");
            
        if !status.success() {
            panic!("windres failed to compile app.rc");
        }
        
        println!("cargo:rustc-link-arg={}", res_path.display());
        println!("cargo:rerun-if-changed=app.rc");
        println!("cargo:rerun-if-changed=app.ico");
    }
}
