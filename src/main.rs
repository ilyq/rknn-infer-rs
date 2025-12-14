use rusqlite::Connection;
use rusqlite::{ffi::sqlite3_auto_extension, Result};
use sqlite_vec::sqlite3_vec_init;
use zerocopy::AsBytes;

fn main() -> Result<()> {
    unsafe {
        sqlite3_auto_extension(Some(std::mem::transmute(sqlite3_vec_init as *const ())));
    }

    let db = Connection::open_in_memory()?;
    let v: Vec<f32> = vec![0.1, 0.2, 0.3];

    let (vec_version, embedding): (String, String) = db.query_row(
        "select  vec_version(), vec_to_json(?)",
        &[v.as_bytes()],
        |x| Ok((x.get(0)?, x.get(1)?)),
    )?;

    println!("vec_version={vec_version}, embedding={embedding}");

    Ok(())
}
