import React, { useState, useEffect } from "react";
import { Check, BookOpen, HelpCircle } from "lucide-react";

export default function StudyCard({
  flashcards = [],
  onMasterCard
}) {
  const [currIdx, setCurrIdx] = useState(0);
  const [isFlipped, setIsFlipped] = useState(false);

  // Reset index when cards change
  useEffect(() => {
    setCurrIdx(0);
    setIsFlipped(false);
  }, [flashcards]);

  if (flashcards.length === 0) {
    return (
      <div className="deck-empty-state" style={{ margin: "auto", textAlign: "center", display: "flex", flexDirection: "column", alignItems: "center", gap: "12px", opacity: 0.6, padding: "40px 10px" }}>
        <BookOpen size={40} style={{ color: "var(--text-dark)" }} />
        <p style={{ fontSize: "0.82rem", color: "var(--text-muted)", lineHeight: 1.5 }}>
          Study flashcards will populate here once you ask a question. They allow you to test your conceptual recall!
        </p>
      </div>
    );
  }

  const currentCard = flashcards[currIdx];

  const handleNext = () => {
    setIsFlipped(false);
    setTimeout(() => {
      setCurrIdx(prev => (prev + 1) % flashcards.length);
    }, 150);
  };

  const handlePrev = () => {
    setIsFlipped(false);
    setTimeout(() => {
      setCurrIdx(prev => (prev - 1 < 0 ? flashcards.length - 1 : prev - 1));
    }, 150);
  };

  return (
    <div style={{ display: "flex", flexDirection: "column", height: "100%", justifyContent: "space-between" }}>
      
      {/* CARD BODY (FLIPPABLE) */}
      <div 
        className={`flashcard-wrapper ${isFlipped ? "flipped" : ""}`}
        onClick={() => setIsFlipped(!isFlipped)}
        style={{ flex: 1, cursor: "pointer", perspective: "1000px", position: "relative", minHeight: "180px" }}
      >
        <div 
          className="flashcard-inner" 
          style={{ 
            position: "absolute", width: "100%", height: "100%", 
            transformStyle: "preserve-3d", transition: "transform 0.4s cubic-bezier(0.4, 0, 0.2, 1)",
            transform: isFlipped ? "rotateY(180deg)" : "rotateY(0deg)"
          }}
        >
          {/* FRONT (QUESTION) */}
          <div 
            className="flashcard-front glass-card" 
            style={{ 
              position: "absolute", width: "100%", height: "100%", 
              backfaceVisibility: "hidden", display: "flex", flexDirection: "column", 
              justifyContent: "center", alignItems: "center", padding: "20px", 
              background: currentCard.mastered ? "rgba(16, 185, 129, 0.05)" : "rgba(255,255,255,0.02)",
              border: currentCard.mastered ? "1px solid rgba(16, 185, 129, 0.2)" : "1px solid var(--border-light)",
              borderRadius: "14px", textAlign: "center"
            }}
          >
            <div style={{ position: "absolute", top: "12px", display: "flex", alignItems: "center", gap: "6px", fontSize: "0.68rem", color: "var(--secondary)", fontFamily: "var(--font-mono)", fontWeight: 700 }}>
              <HelpCircle size={12} /> FLASHCARD QUESTION
            </div>
            <div style={{ fontSize: "0.92rem", fontWeight: 500, color: "white", lineHeight: 1.5 }}>
              {currentCard.question}
            </div>
            <div style={{ position: "absolute", bottom: "12px", fontSize: "0.7rem", color: "var(--text-dark)", textTransform: "uppercase", letterSpacing: "1px" }}>
              Click to Flip
            </div>
            {currentCard.mastered && (
              <div style={{ position: "absolute", top: "12px", right: "12px", color: "var(--accent-green)", display: "flex", alignItems: "center", gap: "3px", fontSize: "0.65rem", fontWeight: 700 }}>
                <Check size={12} /> MASTERED
              </div>
            )}
          </div>

          {/* BACK (ANSWER) */}
          <div 
            className="flashcard-back glass-card" 
            style={{ 
              position: "absolute", width: "100%", height: "100%", 
              backfaceVisibility: "hidden", display: "flex", flexDirection: "column", 
              justifyContent: "center", alignItems: "center", padding: "20px", 
              background: "rgba(255,255,255,0.03)", border: "1px solid var(--border-light)", 
              borderRadius: "14px", transform: "rotateY(180deg)", textAlign: "center"
            }}
          >
            <div style={{ position: "absolute", top: "12px", display: "flex", alignItems: "center", gap: "6px", fontSize: "0.68rem", color: "var(--accent-green)", fontFamily: "var(--font-mono)", fontWeight: 700 }}>
              <BookOpen size={12} /> VERIFIED ANSWER
            </div>
            <div style={{ fontSize: "0.85rem", color: "var(--text-muted)", lineHeight: 1.6, overflowY: "auto", maxHeight: "80%" }}>
              {currentCard.answer}
            </div>
            <div style={{ position: "absolute", bottom: "12px", fontSize: "0.7rem", color: "var(--text-dark)", textTransform: "uppercase", letterSpacing: "1px" }}>
              Click to Flip
            </div>
          </div>

        </div>
      </div>

      {/* CONTROLS */}
      <div style={{ display: "flex", flexDirection: "column", gap: "10px", marginTop: "14px" }}>
        
        {/* Master button */}
        {!currentCard.mastered && (
          <button 
            className="btn btn-primary"
            onClick={(e) => {
              e.stopPropagation();
              onMasterCard(currIdx);
            }}
            style={{ width: "100%", padding: "10px", borderRadius: "10px", display: "flex", gap: "6px", justifyContent: "center", alignItems: "center", cursor: "pointer", background: "rgba(16, 185, 129, 0.1)", border: "1px solid rgba(16, 185, 129, 0.2)", color: "var(--accent-green)" }}
          >
            <Check size={14} /> Mark as Mastered
          </button>
        )}

        {/* Pagination navigation */}
        <div className="deck-navigation" style={{ display: "flex", alignItems: "center", justifyContent: "center", gap: "16px" }}>
          <button className="icon-btn" onClick={handlePrev}>&lt;</button>
          <span style={{ fontSize: "0.78rem", fontFamily: "var(--font-mono)", color: "var(--text-muted)" }}>
            {currIdx + 1} / {flashcards.length}
          </span>
          <button className="icon-btn" onClick={handleNext}>&gt;</button>
        </div>

      </div>

    </div>
  );
}
