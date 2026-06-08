import React, { useState, useRef, useEffect } from "react";
import { BrainCircuit, Lightbulb, Workflow, Send, Terminal, ChevronDown, ChevronRight, User, GraduationCap, Quote } from "lucide-react";

export default function ChatHub({ 
  messages = [], 
  onSendMessage, 
  isLoading = false,
  onCancel
}) {
  const [inputText, setInputText] = useState("");
  const chatMessagesEndRef = useRef(null);
  const inputRef = useRef(null);

  // States for dynamic message details expansion (indexed by message ID/Index + type)
  const [expandedLogs, setExpandedLogs] = useState({});
  const [expandedSources, setExpandedSources] = useState({});

  useEffect(() => {
    // Keep scroll at bottom
    chatMessagesEndRef.current?.scrollIntoView({ behavior: "smooth" });
  }, [messages, isLoading]);

  const handleSend = () => {
    if (!inputText.trim() || isLoading) return;
    onSendMessage(inputText.trim());
    setInputText("");
    if (inputRef.current) {
      inputRef.current.style.height = "auto";
    }
  };

  const handleKeyDown = (e) => {
    if (e.key === "Enter" && !e.shiftKey) {
      e.preventDefault();
      handleSend();
    }
  };

  const handleInput = (e) => {
    setInputText(e.target.value);
    e.target.style.height = "auto";
    e.target.style.height = e.target.scrollHeight + "px";
  };

  const toggleLogs = (idx) => {
    setExpandedLogs(prev => ({ ...prev, [idx]: !prev[idx] }));
  };

  const toggleSources = (idx) => {
    setExpandedSources(prev => ({ ...prev, [idx]: !prev[idx] }));
  };

  // Regex-based simple Markdown formatter
  const formatMarkdown = (text) => {
    if (!text) return "";
    let html = text;
    
    // Escape tags
    html = html.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
    // Code blocks
    html = html.replace(/```([\s\S]*?)```/g, '<pre><code>$1</code></pre>');
    // Inline code
    html = html.replace(/`([^`]+)`/g, '<code>$1</code>');
    // Bold
    html = html.replace(/\*\*([^*]+)\*\*/g, '<strong>$1</strong>');
    // Headers
    html = html.replace(/^### (.*$)/gim, '<h3>$1</h3>');
    html = html.replace(/^## (.*$)/gim, '<h2>$1</h2>');
    html = html.replace(/^# (.*$)/gim, '<h1>$1</h1>');
    // Bullet lists
    html = html.replace(/^\s*-\s+(.*$)/gim, '<ul><li>$1</li></ul>');
    html = html.replace(/^\s*\*\s+(.*$)/gim, '<ul><li>$1</li></ul>');
    html = html.replace(/<\/ul>\s*<ul>/g, "");
    // Numbered lists
    html = html.replace(/^\s*\d+\.\s+(.*$)/gim, '<ol><li>$1</li></ol>');
    html = html.replace(/<\/ol>\s*<ol>/g, "");
    // Line breaks
    html = html.replace(/\n\n/g, '<br><br>');
    
    return { __html: html };
  };

  return (
    <div className="chat-container glass-card" style={{ display: "flex", flexDirection: "column", height: "100%", flex: 1 }}>
      
      {/* Scrollable Message Box */}
      <div className="chat-messages" style={{ flex: 1, overflowY: "auto", padding: "24px", display: "flex", flexDirection: "column", gap: "20px" }}>
        
        {messages.length === 0 ? (
          <div className="welcome-box" style={{ margin: "auto", maxWidth: "550px", textAlign: "center", display: "flex", flexDirection: "column", alignItems: "center", gap: "16px" }}>
            <div className="welcome-icon">
              <BrainCircuit size={32} />
            </div>
            <h2>Welcome to StudyFlow AI</h2>
            <p>Upload your textbook PDFs, syllabus sheets, text logs, or lecture slides on the left, then ask me anything! I will scan your documents, explain the topic, and dynamically draft interactive flashcards and quizzes for you.</p>
            <div className="quick-tips">
              <div className="tip">
                <Lightbulb size={16} /> Use the **Agentic RAG** switch below for complex multi-turn logic, planner agents, and hallucination critics.
              </div>
              <div className="tip">
                <Workflow size={16} /> Connect to Ollama models running locally or configure keys for high-performance cloud APIs in the Settings.
              </div>
            </div>
          </div>
        ) : (
          messages.map((msg, idx) => (
            <div key={idx} className={`message-wrapper ${msg.role}`} style={{ alignSelf: msg.role === "user" ? "flex-end" : "flex-start", maxWidth: msg.role === "user" ? "80%" : "100%" }}>
              <div className="msg-header" style={{ display: "flex", alignItems: "center", gap: "8px", fontSize: "0.75rem", color: "var(--text-muted)", padding: "0 6px", marginBottom: "4px" }}>
                {msg.role === "user" ? (
                  <>
                    <span>You</span> <User size={12} />
                  </>
                ) : (
                  <>
                    <BrainCircuit size={12} style={{ color: "var(--primary)" }} /> <span>StudyFlow Assistant</span>
                  </>
                )}
              </div>
              
              <div className="message-bubble">
                {/* Text Markdown Render */}
                <div className="markdown" dangerouslySetInnerHTML={formatMarkdown(msg.content)} />

                {/* CITATIONS ACCORDION */}
                {msg.sources && msg.sources.length > 0 && (
                  <div className="source-citations" style={{ marginTop: "12px" }}>
                    <div 
                      className={`source-citations-header ${expandedSources[idx] ? "expanded" : ""}`}
                      onClick={() => toggleSources(idx)}
                      style={{ fontSize: "0.78rem", fontWeight: 600, color: "var(--text-dark)", cursor: "pointer", display: "flex", alignItems: "center", gap: "6px" }}
                    >
                      {expandedSources[idx] ? <ChevronDown size={12} style={{ transform: "rotate(180deg)" }} /> : <ChevronRight size={12} />}
                      Scanned Passages ({msg.sources.length})
                    </div>
                    {expandedSources[idx] && (
                      <div className="source-chips expanded" style={{ display: "flex", flexWrap: "wrap", gap: "8px", paddingTop: "8px" }}>
                        {msg.sources.map((src, sIdx) => (
                          <div 
                            key={sIdx} 
                            className="source-chip" 
                            onClick={() => alert(`Context Chunk Passage:\n\n"${src.content}"`)}
                            title={`Relevance Score: ${src.score}`}
                            style={{ display: "flex", alignItems: "center", gap: "6px", background: "rgba(255,255,255,0.02)", border: "1px solid var(--border-light)", padding: "6px 10px", borderRadius: "8px", fontSize: "0.74rem", cursor: "pointer" }}
                          >
                            <Quote size={12} style={{ color: "var(--secondary)" }} />
                            <span>{src.source_name} (Pg. {src.page})</span>
                          </div>
                        ))}
                      </div>
                    )}
                  </div>
                )}

                {/* AGENT SEQUENCE LOGS ACCORDION */}
                {msg.logs && msg.logs.length > 0 && (
                  <div className="agent-logs-container" style={{ marginTop: "10px", background: "rgba(0,0,0,0.25)", border: "1px solid var(--border-light)", borderRadius: "12px", overflow: "hidden" }}>
                    <div 
                      className={`agent-logs-header ${expandedLogs[idx] ? "expanded" : ""}`}
                      onClick={() => toggleLogs(idx)}
                      style={{ display: "flex", justifyContent: "space-between", alignItems: "center", padding: "10px 14px", fontSize: "0.78rem", fontFamily: "var(--font-mono)", color: "var(--secondary)", cursor: "pointer" }}
                    >
                      <span style={{ display: "flex", alignItems: "center", gap: "6px" }}>
                        <Terminal size={12} /> Agent Execution Sequence Logs
                      </span>
                      <ChevronDown size={14} style={{ transform: expandedLogs[idx] ? "rotate(180deg)" : "none", transition: "transform 0.2s" }} />
                    </div>
                    {expandedLogs[idx] && (
                      <div className="agent-logs-body expanded" style={{ display: "flex", flexDirection: "column", gap: "8px", padding: "12px 14px", borderTop: "1px solid var(--border-light)", maxHeight: "200px", overflowY: "auto" }}>
                        {msg.logs.map((log, lIdx) => (
                          <div key={lIdx} className="agent-log-line" style={{ fontFamily: "var(--font-mono)", fontSize: "0.72rem", color: "var(--text-muted)", borderLeft: "2px solid var(--border-glow)", paddingLeft: "8px" }}>
                            {log}
                          </div>
                        ))}
                      </div>
                    )}
                  </div>
                )}
              </div>
            </div>
          ))
        )}

        {/* Dynamic Typing/Thinking Loader */}
        {isLoading && (
          <div className="message-wrapper assistant" style={{ alignSelf: "flex-start" }}>
            <div className="msg-header" style={{ display: "flex", alignItems: "center", gap: "8px", fontSize: "0.75rem", color: "var(--text-muted)", marginBottom: "4px" }}>
              <BrainCircuit size={12} style={{ color: "var(--primary)" }} /> <span>StudyFlow Thinking...</span>
            </div>
            <div className="message-bubble" style={{ background: "rgba(255,255,255,0.03)", border: "1px solid var(--border-light)", padding: "14px 18px", borderRadius: "18px 18px 18px 4px" }}>
              <div className="typing-indicator" style={{ display: "flex", gap: "4px", alignItems: "center", justifyContent: "center", width: "40px", height: "16px" }}>
                <span style={{ width: "6px", height: "6px", background: "var(--text-muted)", borderRadius: "50%", display: "inline-block", animation: "bounce 1.4s infinite ease-in-out both" }}></span>
                <span style={{ width: "6px", height: "6px", background: "var(--text-muted)", borderRadius: "50%", display: "inline-block", animation: "bounce 1.4s infinite ease-in-out both 0.2s" }}></span>
                <span style={{ width: "6px", height: "6px", background: "var(--text-muted)", borderRadius: "50%", display: "inline-block", animation: "bounce 1.4s infinite ease-in-out both 0.4s" }}></span>
              </div>
            </div>
          </div>
        )}

        <div ref={chatMessagesEndRef} />
      </div>

      {/* Input console triggers */}
      <div className="chat-controls" style={{ padding: "16px", background: "rgba(0,0,0,0.25)", borderTop: "1px solid var(--border-light)", borderRadius: "0 0 18px 18px" }}>
        <div className="input-console" style={{ background: "rgba(0,0,0,0.35)", border: "1px solid var(--border-light)", borderRadius: "12px", padding: "8px 12px", display: "flex", alignItems: "center", gap: "12px" }}>
          <textarea 
            ref={inputRef}
            value={inputText}
            onChange={handleInput}
            onKeyDown={handleKeyDown}
            placeholder={isLoading ? "Please wait for response..." : "Ask a study question or request a concept review..."} 
            disabled={isLoading}
            rows={1}
            style={{ flex: 1, background: "none", border: "none", outline: "none", color: "white", fontFamily: "var(--font-outfit)", fontSize: "0.88rem", resize: "none", maxHeight: "120px", padding: "6px 0" }}
          />
          {isLoading ? (
            <button 
              className="btn btn-danger" 
              onClick={onCancel}
              style={{ padding: "8px 16px", borderRadius: "10px", display: "flex", gap: "6px", cursor: "pointer", alignItems: "center", background: "rgba(239, 68, 68, 0.2)", border: "1px solid var(--accent-red)", color: "white" }}
            >
              <span>Stop</span>
              <span style={{ width: "8px", height: "8px", background: "var(--accent-red)", borderRadius: "2px" }}></span>
            </button>
          ) : (
            <button 
              className="btn btn-primary" 
              onClick={handleSend}
              disabled={!inputText.trim()}
              style={{ padding: "8px 16px", borderRadius: "10px", display: "flex", gap: "6px", cursor: "pointer", alignItems: "center" }}
            >
              <span>Send</span> <Send size={12} />
            </button>
          )}
        </div>
      </div>

      {/* Keyframe loader injection styling */}
      <style>{`
        @keyframes bounce {
          0%, 80%, 100% { transform: scale(0); }
          40% { transform: scale(1.0); }
        }
      `}</style>
    </div>
  );
}
