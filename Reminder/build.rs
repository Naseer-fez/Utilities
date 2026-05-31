// build.rs — Compile Slint UI files and Windows resources at build time
fn main() {
    slint_build::compile("ui/popup.slint").expect("Failed to compile popup");
    slint_build::compile("ui/editor.slint").expect("Failed to compile editor");

    #[cfg(target_os = "windows")]
    {
        let mut res = winres::WindowsResource::new();
        res.set_icon("assets/icon.ico");
        res.compile().expect("Failed to compile Windows resource file");
    }
}
