import React, { useRef } from "react";
import { Folder, Upload, Trash2, ShieldAlert, FileText, Scan } from "lucide-react";

export default function DocManager({
  files = [],
  scanPath = "",
  onScanFolder,
  onUploadFile,
  onDeleteFile,
  onWipeDatabase
}) {
  const fileInputRef = useRef(null);

  const handleFileChange = (e) => {
    if (e.target.files && e.target.files[0]) {
      onUploadFile(e.target.files[0]);
    }
  };

  return (
    <aside className="sidebar doc-manager glass-card" style={{ display: "flex", flexDirection: "column", padding: "18px", gap: "16px", height: "100%" }}>
      <div className="sidebar-header" style={{ borderBottom: "1px solid var(--border-light)", paddingBottom: "12px" }}>
        <h2 style={{ fontSize: "1.1rem", fontWeight: 600, display: "flex", alignItems: "center", gap: "8px", color: "white" }}>
          <Folder size={18} style={{ color: "var(--primary)" }} /> Study Files
        </h2>
        <p className="desc" style={{ fontSize: "0.72rem", color: "var(--text-dark)", textTransform: "uppercase", fontFamily: "var(--font-mono)", marginTop: "3px" }}>
          RAG Vector Index Manager
        </p>
      </div>

      {/* Directory path */}
      <div className="path-box" style={{ display: "flex", flexDirection: "column", gap: "4px" }}>
        <span style={{ fontSize: "0.74rem", color: "var(--text-muted)", fontFamily: "var(--font-mono)" }}>SCANNING PATH:</span>
        <div style={{ background: "rgba(0,0,0,0.2)", border: "1px solid var(--border-light)", padding: "8px 10px", borderRadius: "8px", fontSize: "0.75rem", color: "var(--text-muted)", wordBreak: "break-all", fontFamily: "var(--font-mono)" }}>
          {scanPath || "d:\\AI\\Agents\\study_docs"}
        </div>
      </div>

      {/* Actions row */}
      <div style={{ display: "grid", gridTemplateColumns: "1fr 1fr", gap: "10px" }}>
        <button className="btn btn-secondary" onClick={onScanFolder} style={{ display: "flex", gap: "6px", alignItems: "center", justifyContent: "center", fontSize: "0.8rem", padding: "8px 12px", borderRadius: "8px", cursor: "pointer" }}>
          <Scan size={14} /> Scan Folder
        </button>
        <button 
          className="btn btn-secondary" 
          onClick={() => fileInputRef.current?.click()} 
          style={{ display: "flex", gap: "6px", alignItems: "center", justifyContent: "center", fontSize: "0.8rem", padding: "8px 12px", borderRadius: "8px", cursor: "pointer" }}
        >
          <Upload size={14} /> Upload File
        </button>
        <input 
          type="file" 
          ref={fileInputRef} 
          onChange={handleFileChange} 
          style={{ display: "none" }} 
          accept=".pdf,.txt,.md,.json,.csv"
        />
      </div>

      {/* Indexed Files list */}
      <div style={{ flex: 1, display: "flex", flexDirection: "column", minHeight: 0, gap: "8px" }}>
        <div style={{ fontSize: "0.74rem", color: "var(--text-muted)", fontFamily: "var(--font-mono)", display: "flex", justifyContent: "space-between" }}>
          <span>INDEXED FILES ({files.length}):</span>
        </div>
        
        <div className="file-list" style={{ flex: 1, overflowY: "auto", display: "flex", flexDirection: "column", gap: "8px", paddingRight: "4px" }}>
          {files.length === 0 ? (
            <div style={{ margin: "auto", textAlign: "center", color: "var(--text-dark)", padding: "20px 10px", fontSize: "0.8rem" }}>
              No study files indexed. Click Upload or Scan Folder to begin.
            </div>
          ) : (
            files.map((file, idx) => (
              <div 
                key={idx} 
                className="file-item" 
                style={{ display: "flex", alignItems: "center", justifyContent: "space-between", background: "rgba(255,255,255,0.02)", border: "1px solid var(--border-light)", padding: "8px 10px", borderRadius: "10px", gap: "8px" }}
              >
                <div style={{ display: "flex", alignItems: "center", gap: "8px", minWidth: 0 }}>
                  <FileText size={16} style={{ color: "var(--secondary)", flexShrink: 0 }} />
                  <div style={{ display: "flex", flexDirection: "column", minWidth: 0 }}>
                    <span style={{ fontSize: "0.82rem", color: "white", textOverflow: "ellipsis", overflow: "hidden", whiteSpace: "nowrap" }} title={file.filename}>
                      {file.filename}
                    </span>
                    <span style={{ fontSize: "0.68rem", color: "var(--text-dark)", fontFamily: "var(--font-mono)" }}>
                      {file.chunk_count} passages
                    </span>
                  </div>
                </div>
                <button 
                  onClick={() => onDeleteFile(file.filename)} 
                  style={{ background: "none", border: "none", color: "var(--accent-red)", opacity: 0.7, cursor: "pointer", padding: "4px" }}
                  title="Remove file from index"
                >
                  <Trash2 size={14} />
                </button>
              </div>
            ))
          )}
        </div>
      </div>

      {/* Clear Database button */}
      <button 
        className="btn btn-danger" 
        onClick={onWipeDatabase} 
        style={{ display: "flex", gap: "6px", alignItems: "center", justifyContent: "center", fontSize: "0.8rem", padding: "10px", borderRadius: "10px", cursor: "pointer", border: "1px solid rgba(239, 68, 68, 0.2)", background: "rgba(239, 68, 68, 0.05)", color: "var(--accent-red)", marginTop: "auto" }}
      >
        <ShieldAlert size={14} /> Wipe database index
      </button>
    </aside>
  );
}
