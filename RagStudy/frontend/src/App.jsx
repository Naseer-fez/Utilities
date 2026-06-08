import React, { useState, useEffect } from "react";
import DocManager from "./components/DocManager";
import ChatHub from "./components/ChatHub";
import SettingsBar from "./components/SettingsBar";
import StudyCard from "./cards/StudyCard";
import { GraduationCap, Layers, BookOpen, CheckSquare, Info, AlertTriangle, CheckCircle2, RotateCcw } from "lucide-react";

// Dynamic API Routing - supports both Hot-Reload Vite (5173) and production FastAPI (8000)
const API_BASE = window.location.port === "5173" 
  ? "http://127.0.0.1:8000" 
  : "";

export default function App() {
  // Global API states
  const [ollamaStatus, setOllamaStatus] = useState("offline");
  const [ollamaModels, setOllamaModels] = useState([]);
  const [indexedFiles, setIndexedFiles] = useState([]);
  const [scanPath, setScanPath] = useState("d:\\AI\\Agents\\study_docs");

  // Selection configurations
  const [provider, setProvider] = useState("ollama");
  const [activeModel, setActiveModel] = useState("");
  const [topK, setTopK] = useState(4);
  const [enableAgentic, setEnableAgentic] = useState(true);

  // Chat message histories
  const [messages, setMessages] = useState([]);
  const [isLoading, setIsLoading] = useState(false);

  // Active study deck tabs & lists
  const [activeTab, setActiveTab] = useState("flashcards");
  const [flashcards, setFlashcards] = useState([]);
  const [quizzes, setQuizzes] = useState([]);

  // Quiz interactive state
  const [activeQuizIdx, setActiveQuizIdx] = useState(0);
  const [quizScores, setQuizScores] = useState({}); // { questionIdx: { selected, isCorrect } }

  // Visual dynamic Notification Toasts
  const [toast, setToast] = useState({ show: false, message: "", type: "info" });

  // AbortController ref for cancel generation support
  const abortControllerRef = React.useRef(null);

  const handleCancelGeneration = () => {
    if (abortControllerRef.current) {
      abortControllerRef.current.abort();
      abortControllerRef.current = null;
    }
  };

  useEffect(() => {
    checkServerStatus(true);
    fetchIndexedFiles();
  }, []);

  const showToast = (message, type = "info") => {
    setToast({ show: true, message, type });
    setTimeout(() => {
      setToast(prev => ({ ...prev, show: false }));
    }, 4000);
  };

  // Check connection status
  const checkServerStatus = async (isInitial = false) => {
    try {
      const res = await fetch(`${API_BASE}/api/status`);
      if (!res.ok) throw new Error();
      const data = await res.json();
      
      if (data.config.study_docs_dir) {
        setScanPath(data.config.study_docs_dir);
      }

      if (data.ollama.status === "online") {
        setOllamaStatus("online");
        setOllamaModels(data.ollama.models);
        
        // Auto-select first local model if not yet set (preferring local-only models over cloud subscription ones)
        if (provider === "ollama" && data.ollama.models.length > 0) {
          const localOnly = data.ollama.models.find(m => !m.includes("-cloud"));
          setActiveModel(localOnly || data.ollama.models[0]);
        }
        if (isInitial) showToast("Connected to local Ollama successfully!", "success");
      } else {
        setOllamaStatus("offline");
        setOllamaModels([]);
        if (isInitial) showToast("Local Ollama is offline. Pull models or configure cloud APIs.", "warning");
      }
    } catch (err) {
      setOllamaStatus("offline");
      showToast("Cannot connect to FastAPI server. Run start.bat launcher.", "error");
    }
  };

  // Get index files list
  const fetchIndexedFiles = async () => {
    try {
      const res = await fetch(`${API_BASE}/api/files`);
      if (!res.ok) throw new Error();
      const data = await res.json();
      setIndexedFiles(data);
    } catch (err) {
      showToast("Failed to fetch study documents index.", "error");
    }
  };

  // Update default models based on selected provider
  const handleProviderChange = (selectedProvider) => {
    setProvider(selectedProvider);
    
    // Choose sensible default model
    if (selectedProvider === "ollama") {
      if (ollamaModels.length > 0) {
        const localOnly = ollamaModels.find(m => !m.includes("-cloud"));
        setActiveModel(localOnly || ollamaModels[0]);
      } else {
        setActiveModel("deepseek-r1:8b");
      }
    } else if (selectedProvider === "studylm") {
      setActiveModel("studylm-1.0-scratch");
    } else if (selectedProvider === "gemini") {
      setActiveModel("gemini-1.5-flash");
    } else if (selectedProvider === "openai") {
      setActiveModel("gpt-4o-mini");
    } else if (selectedProvider === "anthropic") {
      setActiveModel("claude-3-5-sonnet-20240620");
    }
    
    showToast(`Model provider changed to ${selectedProvider}`, "info");
  };

  // Upload dynamic documents
  const handleUploadFile = async (file) => {
    const formData = new FormData();
    formData.append("file", file);
    formData.append("embedding_mode", provider);

    try {
      const res = await fetch(`${API_BASE}/api/upload`, {
        method: "POST",
        body: formData
      });
      
      if (res.ok) {
        const data = await res.json();
        showToast(`Indexed ${file.name} successfully into ${data.chunk_count} passages!`, "success");
        fetchIndexedFiles();
      } else {
        showToast(`Failed to parse file: ${file.name}`, "error");
      }
    } catch (err) {
      showToast(`Network error uploading file: ${file.name}`, "error");
    }
  };

  // Trigger local folder scan
  const handleScanFolder = async () => {
    try {
      const res = await fetch(`${API_BASE}/api/scan`, { method: "POST" });
      if (!res.ok) throw new Error();
      
      const data = await res.json();
      if (data.processed && data.processed.length > 0) {
        showToast(`Scanned and indexed ${data.processed.length} new study guides!`, "success");
        fetchIndexedFiles();
      } else {
        showToast("Scan complete. No new study files found.", "info");
      }
    } catch (err) {
      showToast("Error scanning local folder.", "error");
    }
  };

  // Delete file index record
  const handleDeleteFile = async (filename) => {
    if (!confirm(`Are you sure you want to delete indexed records for '${filename}'?`)) return;

    try {
      const res = await fetch(`${API_BASE}/api/files/${encodeURIComponent(filename)}`, { method: "DELETE" });
      if (res.ok) {
        showToast(`Removed file ${filename} from database.`, "info");
        fetchIndexedFiles();
      } else {
        showToast("Failed to delete record.", "error");
      }
    } catch (err) {
      showToast("Error deleting indexed file.", "error");
    }
  };

  // Wipe index
  const handleWipeDatabase = async () => {
    if (!confirm("WARNING: This will clear ALL files and indices from your RAG database! Proceed?")) return;

    try {
      const res = await fetch(`${API_BASE}/api/clear`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ confirm: true })
      });
      if (res.ok) {
        showToast("Vector database successfully wiped.", "success");
        fetchIndexedFiles();
        setFlashcards([]);
        setQuizzes([]);
      }
    } catch (err) {
      showToast("Error wiping RAG database.", "error");
    }
  };

  // Chat message query
  const handleSendMessage = async (text) => {
    setIsLoading(true);
    
    // Add user message bubble
    setMessages(prev => [...prev, { role: "user", content: text }]);

    const payload = {
      query: text,
      provider,
      model: activeModel,
      temperature: 0.5,
      top_k: topK,
      enable_agentic_flow: enableAgentic
    };

    // Initialize new AbortController
    const controller = new AbortController();
    abortControllerRef.current = controller;

    try {
      const res = await fetch(`${API_BASE}/api/query`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(payload),
        signal: controller.signal
      });

      if (!res.ok) {
        let errorMsg = "Query execution failed.";
        try {
          const errorData = await res.json();
          errorMsg = errorData.detail || errorMsg;
        } catch (_) {}
        throw new Error(errorMsg);
      }

      // Read response stream line-by-line using SSE reader
      const reader = res.body.getReader();
      const decoder = new TextDecoder();
      let buffer = "";

      // Append empty assistant message that we will stream into
      setMessages(prev => [...prev, { 
        role: "assistant", 
        content: "",
        sources: [],
        logs: []
      }]);

      while (true) {
        const { value, done } = await reader.read();
        if (done) break;

        buffer += decoder.decode(value, { stream: true });
        const lines = buffer.split("\n\n");
        buffer = lines.pop() || "";

        for (const line of lines) {
          const trimmed = line.trim();
          if (trimmed.startsWith("data: ")) {
            try {
              const event = JSON.parse(trimmed.slice(6));
              
              if (event.type === "log") {
                setMessages(prev => {
                  const copy = [...prev];
                  const lastMsg = copy[copy.length - 1];
                  if (lastMsg && lastMsg.role === "assistant") {
                    lastMsg.logs = [...(lastMsg.logs || []), event.content];
                  }
                  return copy;
                });
              } else if (event.type === "sources") {
                setMessages(prev => {
                  const copy = [...prev];
                  const lastMsg = copy[copy.length - 1];
                  if (lastMsg && lastMsg.role === "assistant") {
                    lastMsg.sources = event.content;
                  }
                  return copy;
                });
              } else if (event.type === "answer") {
                setMessages(prev => {
                  const copy = [...prev];
                  const lastMsg = copy[copy.length - 1];
                  if (lastMsg && lastMsg.role === "assistant") {
                    lastMsg.content += event.content;
                  }
                  return copy;
                });
              } else if (event.type === "study_deck") {
                const deck = event.content;
                if (deck) {
                  if (deck.flashcards) {
                    setFlashcards(deck.flashcards.map(fc => ({ ...fc, mastered: false })));
                  }
                  if (deck.quiz) {
                    setQuizzes(deck.quiz);
                    setActiveQuizIdx(0);
                    setQuizScores({});
                  }
                }
              }
            } catch (err) {
              console.error("Error decoding SSE line:", trimmed, err);
            }
          }
        }
      }

    } catch (err) {
      if (err.name === "AbortError") {
        setMessages(prev => {
          const copy = [...prev];
          const lastMsg = copy[copy.length - 1];
          if (lastMsg && lastMsg.role === "assistant") {
            lastMsg.content += "\n\n*[Generation stopped by user]*";
          }
          return copy;
        });
        showToast("Generation cancelled by user.", "warning");
      } else {
        setMessages(prev => [...prev, { 
          role: "assistant", 
          content: `Error generating response: ${err.message}` 
        }]);
        showToast("Query failed. Please verify configurations.", "error");
      }
    } finally {
      setIsLoading(false);
      abortControllerRef.current = null;
    }
  };

  // Swipe-to-master card handler
  const handleMasterCard = (idx) => {
    setFlashcards(prev => {
      const updated = [...prev];
      if (updated[idx]) {
        updated[idx].mastered = true;
        showToast(`Mastered Flashcard: "${updated[idx].question.substring(0, 30)}..."!`, "success");
      }
      return updated;
    });
  };

  // Multiple choice quiz selection evaluator
  const handleQuizSelection = (questionIdx, selectedOption, correctOption) => {
    if (quizScores[questionIdx]) return; // Avoid double clicking
    
    const isCorrect = (selectedOption === correctOption);
    setQuizScores(prev => ({
      ...prev,
      [questionIdx]: { selected: selectedOption, isCorrect }
    }));
  };

  // Visual toast icons
  const getToastIcon = () => {
    if (toast.type === "success") return <CheckCircle2 size={16} style={{ color: "var(--accent-green)" }} />;
    if (toast.type === "warning") return <AlertTriangle size={16} style={{ color: "var(--accent-orange)" }} />;
    if (toast.type === "error") return <AlertTriangle size={16} style={{ color: "var(--accent-red)" }} />;
    return <Info size={16} style={{ color: "var(--secondary)" }} />;
  };

  return (
    <div style={{ position: "relative", height: "100vh", width: "100vw" }}>
      {/* Background blur overlays */}
      <div className="glow-bg glow-1"></div>
      <div className="glow-bg glow-2"></div>

      <div className="app-container" style={{ display: "flex", flexDirection: "column", height: "100vh", padding: "16px", gap: "16px" }}>
        
        {/* APP HEADER */}
        <header className="app-header" style={{ display: "flex", justifyContent: "space-between", alignItems: "center", padding: "10px 20px" }}>
          <div className="brand" style={{ display: "flex", alignItems: "center", gap: "12px" }}>
            <div className="brand-logo" style={{ width: "42px", height: "42px", borderRadius: "12px", display: "flex", alignItems: "center", justifyContent: "center" }}>
              <GraduationCap size={22} style={{ color: "white" }} />
            </div>
            <div className="brand-info">
              <h1 style={{ fontSize: "1.45rem", fontWeight: 700, color: "white" }}>StudyFlow AI</h1>
              <span className="sub" style={{ fontSize: "0.78rem", color: "var(--text-muted)", fontFamily: "var(--font-mono)" }}>Stateful Agentic Study Space</span>
            </div>
          </div>

          <div style={{ display: "flex", alignItems: "center", gap: "14px" }}>
            <div className={`status-badge ${ollamaStatus}`}>
              <span className="pulse-dot"></span>
              <span className="status-text">{ollamaStatus === "online" ? "Ollama Active" : "Ollama Offline"}</span>
            </div>
          </div>
        </header>

        {/* WORKSPACE FLEXGRID ROW */}
        <div className="app-workspace" style={{ display: "grid", gridTemplateColumns: "290px 1fr 340px", flex: 1, gap: "16px", minHeight: 0 }}>
          
          {/* Doc Manager Panel */}
          <DocManager 
            files={indexedFiles} 
            scanPath={scanPath}
            onScanFolder={handleScanFolder}
            onUploadFile={handleUploadFile}
            onDeleteFile={handleDeleteFile}
            onWipeDatabase={handleWipeDatabase}
          />

          {/* Central Chat & Settings Frame */}
          <div style={{ display: "flex", flexDirection: "column", gap: "16px", height: "100%", minHeight: 0 }}>
            <SettingsBar 
              ollamaStatus={ollamaStatus}
              ollamaModels={ollamaModels}
              provider={provider}
              model={activeModel}
              topK={topK}
              enableAgentic={enableAgentic}
              onProviderChange={handleProviderChange}
              onModelChange={setActiveModel}
              onTopKChange={setTopK}
              onAgenticChange={setEnableAgentic}
              onRefreshStatus={() => checkServerStatus(false)}
            />
            
            <ChatHub 
              messages={messages}
              onSendMessage={handleSendMessage}
              isLoading={isLoading}
              onCancel={handleCancelGeneration}
            />
          </div>

          {/* Right Study Aids panel */}
          <aside className="sidebar study-deck glass-card" style={{ display: "flex", flexDirection: "column", padding: "18px", gap: "14px", height: "100%" }}>
            <div className="study-deck-header" style={{ borderBottom: "1px solid var(--border-light)", paddingBottom: "12px" }}>
              <h2 style={{ fontSize: "1.1rem", fontWeight: 600, display: "flex", alignItems: "center", gap: "8px", color: "white" }}>
                <Layers size={18} style={{ color: "var(--secondary)" }} /> Study Deck
              </h2>
              <p className="desc" style={{ fontSize: "0.72rem", color: "var(--text-dark)", textTransform: "uppercase", fontFamily: "var(--font-mono)", marginTop: "3px" }}>
                EXTRACTED FROM CHAT RESULTS
              </p>
            </div>

            {/* Tab switchers */}
            <div className="deck-tabs" style={{ display: "grid", gridTemplateColumns: "1fr 1fr", background: "rgba(0,0,0,0.2)", border: "1px solid var(--border-light)", borderRadius: "10px", padding: "3px" }}>
              <button 
                className={`deck-tab ${activeTab === "flashcards" ? "active" : ""}`}
                onClick={() => setActiveTab("flashcards")}
              >
                <BookOpen size={12} /> Flashcards
              </button>
              <button 
                className={`deck-tab ${activeTab === "quiz" ? "active" : ""}`}
                onClick={() => setActiveTab("quiz")}
              >
                <CheckSquare size={12} /> Self-Check Quiz
              </button>
            </div>

            <div className="deck-content" style={{ flex: 1, minHeight: 0, marginTop: "14px", display: "flex", flexDirection: "column" }}>
              
              {/* TAB 1: FLASHCARDS STACK */}
              {activeTab === "flashcards" && (
                <StudyCard 
                  flashcards={flashcards}
                  onMasterCard={handleMasterCard}
                />
              )}

              {/* TAB 2: INTERACTIVE SELF-CHECK QUIZ */}
              {activeTab === "quiz" && (
                <div style={{ display: "flex", flexDirection: "column", height: "100%", justifyContent: "space-between" }}>
                  {quizzes.length === 0 ? (
                    <div className="deck-empty-state">
                      <CheckSquare size={40} style={{ opacity: 0.4, color: "var(--text-dark)" }} />
                      <p>Self-check quizzes will populate here once study questions are asked to test your conceptual knowledge.</p>
                    </div>
                  ) : (
                    <div className="quiz-panel" style={{ display: "flex", flexDirection: "column", height: "100%", justifyContent: "space-between" }}>
                      <div>
                        <div className="quiz-question-number" style={{ fontFamily: "var(--font-mono)", fontSize: "0.7rem", color: "var(--secondary)", fontWeight: 700, textTransform: "uppercase" }}>
                          QUESTION {activeQuizIdx + 1} / {quizzes.length}
                        </div>
                        <div className="quiz-question-text" style={{ fontSize: "0.92rem", fontWeight: 500, color: "white", marginTop: "6px", marginBottom: "16px", lineHeight: 1.5 }}>
                          {quizzes[activeQuizIdx].question}
                        </div>
                        
                        {/* Options Buttons */}
                        <div className="quiz-options" style={{ display: "flex", flexDirection: "column", gap: "8px" }}>
                          {quizzes[activeQuizIdx].options.map((opt) => {
                            const isAnswered = !!quizScores[activeQuizIdx];
                            const selected = quizScores[activeQuizIdx]?.selected;
                            
                            let optClass = "";
                            if (isAnswered) {
                              if (opt === quizzes[activeQuizIdx].answer) optClass = "correct";
                              else if (opt === selected) optClass = "incorrect";
                            }

                            return (
                              <button
                                key={opt}
                                className={`quiz-option ${optClass}`}
                                disabled={isAnswered}
                                onClick={() => handleQuizSelection(activeQuizIdx, opt, quizzes[activeQuizIdx].answer)}
                              >
                                {opt}
                              </button>
                            );
                          })}
                        </div>

                        {/* Feedback Explanations */}
                        {quizScores[activeQuizIdx] && (
                          <div className={`quiz-feedback card ${quizScores[activeQuizIdx].isCorrect ? "correct" : "incorrect"}`} style={{ display: "flex", flexDirection: "column", gap: "4px", padding: "10px 14px", marginTop: "14px", background: "rgba(0,0,0,0.3)" }}>
                            <div className="feedback-title" style={{ fontSize: "0.78rem", fontWeight: 700, color: quizScores[activeQuizIdx].isCorrect ? "var(--accent-green)" : "var(--accent-red)" }}>
                              {quizScores[activeQuizIdx].isCorrect ? "✓ Correct!" : "✗ Incorrect"}
                            </div>
                            <div className="feedback-text" style={{ fontSize: "0.72rem", color: "var(--text-muted)", lineHeight: 1.4 }}>
                              {quizzes[activeQuizIdx].explanation}
                            </div>
                          </div>
                        )}
                      </div>

                      {/* Quiz pagination */}
                      <div className="deck-navigation" style={{ display: "flex", alignItems: "center", justifyContent: "center", gap: "16px", marginTop: "14px" }}>
                        <button 
                          className="icon-btn" 
                          onClick={() => setActiveQuizIdx(prev => (prev - 1 < 0 ? quizzes.length - 1 : prev - 1))}
                        >
                          &lt;
                        </button>
                        <span className="nav-indicator" style={{ fontFamily: "var(--font-mono)", fontSize: "0.78rem" }}>
                          {activeQuizIdx + 1} / {quizzes.length}
                        </span>
                        <button 
                          className="icon-btn" 
                          onClick={() => setActiveQuizIdx(prev => (prev + 1 >= quizzes.length ? 0 : prev + 1))}
                        >
                          &gt;
                        </button>
                      </div>

                    </div>
                  )}
                </div>
              )}

            </div>
          </aside>

        </div>
      </div>

      {/* Notification Toast Alert overlay */}
      <div className={`toast ${toast.show ? "show" : ""}`} style={{ display: "flex", gap: "10px", alignItems: "center" }}>
        {getToastIcon()}
        <span className="toast-message">{toast.message}</span>
      </div>
    </div>
  );
}
