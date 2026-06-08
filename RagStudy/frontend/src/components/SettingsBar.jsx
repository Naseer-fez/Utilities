import React from "react";
import { Sliders, Cpu, RefreshCw, Zap } from "lucide-react";

export default function SettingsBar({
  ollamaStatus = "offline",
  ollamaModels = [],
  provider = "ollama",
  model = "",
  topK = 4,
  enableAgentic = true,
  onProviderChange,
  onModelChange,
  onTopKChange,
  onAgenticChange,
  onRefreshStatus
}) {
  return (
    <div className="settings-bar glass-card" style={{ display: "flex", gap: "16px", padding: "12px 18px", alignItems: "center", justifyContent: "space-between", flexWrap: "wrap" }}>
      
      {/* Provider Selector */}
      <div style={{ display: "flex", alignItems: "center", gap: "8px" }}>
        <Cpu size={16} style={{ color: "var(--primary)" }} />
        <span style={{ fontSize: "0.82rem", color: "var(--text-muted)" }}>PROVIDER:</span>
        <select 
          value={provider} 
          onChange={(e) => onProviderChange(e.target.value)}
          style={{ background: "rgba(0,0,0,0.3)", color: "white", border: "1px solid var(--border-light)", padding: "6px 12px", borderRadius: "8px", fontSize: "0.82rem", outline: "none", cursor: "pointer" }}
        >
          <option value="ollama">Ollama (Local)</option>
          <option value="studylm">StudyLM (Custom Local)</option>
          <option value="gemini">Google Gemini</option>
          <option value="openai">OpenAI GPT</option>
          <option value="anthropic">Anthropic Claude</option>
        </select>
      </div>

      {/* Model Selector */}
      <div style={{ display: "flex", alignItems: "center", gap: "8px", minWidth: "160px" }}>
        <span style={{ fontSize: "0.82rem", color: "var(--text-muted)" }}>MODEL:</span>
        {provider === "ollama" ? (
          <div style={{ display: "flex", alignItems: "center", gap: "6px" }}>
            <select
              value={model}
              onChange={(e) => onModelChange(e.target.value)}
              style={{ background: "rgba(0,0,0,0.3)", color: "white", border: "1px solid var(--border-light)", padding: "6px 12px", borderRadius: "8px", fontSize: "0.82rem", outline: "none", cursor: "pointer", maxWidth: "160px" }}
            >
              {ollamaModels.length === 0 ? (
                <option value="">No local models</option>
              ) : (
                ollamaModels.map(m => <option key={m} value={m}>{m}</option>)
              )}
            </select>
            <button 
              onClick={onRefreshStatus} 
              style={{ background: "none", border: "none", color: "var(--text-muted)", cursor: "pointer", display: "flex", alignItems: "center", padding: "4px" }}
              title="Refresh models list"
            >
              <RefreshCw size={14} className={ollamaStatus === "checking" ? "spin" : ""} />
            </button>
          </div>
        ) : (
          <input 
            type="text" 
            value={model} 
            onChange={(e) => onModelChange(e.target.value)}
            placeholder="Enter model name..."
            style={{ background: "rgba(0,0,0,0.3)", color: "white", border: "1px solid var(--border-light)", padding: "6px 12px", borderRadius: "8px", fontSize: "0.82rem", outline: "none", width: "160px" }}
          />
        )}
      </div>

      {/* Top K Search settings */}
      <div style={{ display: "flex", alignItems: "center", gap: "8px" }}>
        <Sliders size={14} style={{ color: "var(--secondary)" }} />
        <span style={{ fontSize: "0.82rem", color: "var(--text-muted)" }}>RAG CHUNKS (K): {topK}</span>
        <input 
          type="range" 
          min="1" 
          max="8" 
          value={topK} 
          onChange={(e) => onTopKChange(parseInt(e.target.value))}
          style={{ width: "80px", cursor: "pointer" }}
        />
      </div>

      {/* Agentic workflow switch */}
      <div style={{ display: "flex", alignItems: "center", gap: "10px" }}>
        <Zap size={14} style={{ color: enableAgentic ? "var(--accent-orange)" : "var(--text-dark)" }} />
        <span style={{ fontSize: "0.82rem", color: "var(--text-muted)" }}>AGENTIC RAG:</span>
        <label className="switch" style={{ position: "relative", display: "inline-block", width: "38px", height: "20px" }}>
          <input 
            type="checkbox" 
            checked={enableAgentic} 
            onChange={(e) => onAgenticChange(e.target.checked)} 
            style={{ opacity: 0, width: 0, height: 0 }}
          />
          <span 
            className="slider" 
            style={{ 
              position: "absolute", 
              cursor: "pointer", 
              top: 0, left: 0, right: 0, bottom: 0, 
              background: enableAgentic ? "var(--secondary)" : "rgba(255,255,255,0.1)", 
              transition: "0.3s", 
              borderRadius: "20px" 
            }}
          >
            <span 
              style={{ 
                position: "absolute", 
                height: "14px", width: "14px", 
                left: enableAgentic ? "20px" : "3px", bottom: "3px", 
                background: "white", 
                transition: "0.3s", 
                borderRadius: "50%" 
              }}
            ></span>
          </span>
        </label>
      </div>

    </div>
  );
}
