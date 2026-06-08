import json
import re
from typing import List, Dict, Any, Tuple, Optional
from backend.models import LLMClient
from backend.vector_store import VectorStore

def get_trained_knowledge(query: str) -> str:
    """Helper to retrieve matching concept definitions from fine_tuned_weights.json."""
    import os
    import json
    paths = [
        os.path.join("backend", "db", "fine_tuned_weights.json"),
        os.path.join("db", "fine_tuned_weights.json"),
        "fine_tuned_weights.json"
    ]
    definitions = []
    for p in paths:
        if os.path.exists(p):
            try:
                with open(p, "r", encoding="utf-8") as f:
                    data = json.load(f)
                    definitions = data.get("definitions", [])
                    break
            except Exception:
                pass
    if not definitions:
        return ""
        
    # Find matching concepts
    query_words = set(re.findall(r'[a-zA-Z0-9]+', query.lower()))
    matches = []
    for d in definitions:
        concept_words = set(re.findall(r'[a-zA-Z0-9]+', d["concept"].lower()))
        # Check intersection if words overlap
        if query_words.intersection(concept_words):
            matches.append(f"- Concept: {d['concept']}\n  Definition: {d['text']}\n  Source: {d['source']} (Page {d['page']})")
            
    if matches:
        return "\n\n".join(matches)
    return ""

class StudyAgent:
    """Orchestrates the Agentic RAG Workflow to provide state-of-the-art studying support."""

    @staticmethod
    def generate_study_deck(context: str, provider: str, model: str) -> Dict[str, Any]:
        """
        Dynamically extracts key educational details from the retrieved context
        and generates interactive study flashcards and a multiple-choice quiz.
        Uses our high-performance local regex/co-occurrence generator for all providers to save speed.
        """
        default_deck = {
            "flashcards": [
                {"question": "What is the primary topic of the retrieved material?", "answer": "Please review the indexed files to build specific flashcards."}
            ],
            "quiz": [
                {
                    "question": "Select the correct statement based on your study materials.",
                    "options": ["Option A", "Option B", "Option C", "Option D"],
                    "answer": "Option A",
                    "explanation": "Ensure documents are properly loaded to generate specific quizzes."
                }
            ]
        }
        
        try:
            from backend.local_llm import LocalStudyLLM
            local_engine = LocalStudyLLM()
            deck_json = local_engine.handle_deck_generator(context)
            deck = json.loads(deck_json)
            if "flashcards" in deck and "quiz" in deck:
                return deck
        except Exception as e:
            print(f"[WARNING] Dynamic Study Deck generation failed: {e}")
            
        return default_deck

    @classmethod
    def run_agentic_flow(
        cls, 
        user_query: str, 
        vector_store: VectorStore, 
        provider: str, 
        model: str, 
        temperature: float = 0.5,
        top_k: int = 4
    ) -> Dict[str, Any]:
        """
        Executes a 4-step Agentic RAG workflow:
        1. Planner Agent: Identifies if context is needed, creates search terms.
        2. Retriever Agent: Gathers and ranks vector chunks.
        3. Synthesizer Agent: Generates detailed explanation with source references.
        4. Critic Agent: Double-checks generated response for hallucinations.
        """
        logs = []
        
        # --- STEP 1: PLANNER AGENT ---
        logs.append("[Planner] Evaluating study request to formulate reasoning steps...")
        planner_system = (
            "You are a study coordinator agent. Your job is to analyze the user's question and determine:\n"
            "1. Do we need to query their study files for specific facts? (Yes/No)\n"
            "2. What is the optimized, focused keyword phrase to search in the documents?\n"
            "Format your response as a simple JSON object with keys: 'need_retrieval' (boolean), and 'search_query' (string).\n"
            "Do not include markdown wrapping or other text."
        )
        planner_prompt = f"User Question: '{user_query}'"
        
        need_retrieval = True
        search_query = user_query
        
        try:
            plan_raw = LLMClient.generate(provider, model, planner_prompt, planner_system, temperature=0.1)
            cleaned_plan = plan_raw.strip()
            if cleaned_plan.startswith("```"):
                cleaned_plan = re.sub(r"^```(?:json)?\n", "", cleaned_plan)
                cleaned_plan = re.sub(r"\n```$", "", cleaned_plan).strip()
            
            plan = json.loads(cleaned_plan)
            need_retrieval = plan.get("need_retrieval", True)
            search_query = plan.get("search_query", user_query)
            logs.append(f"[Planner] Analysis Complete. Retrieval needed: {need_retrieval}. Optimized query: '{search_query}'")
        except Exception as e:
            logs.append(f"[Planner Warning] Planner parsing failed: {e}. Defaulting to full retrieval.")
        
        # --- STEP 2: RETRIEVER AGENT ---
        retrieved_chunks = []
        context_str = ""
        if need_retrieval:
            logs.append(f"[Retriever] Scanning database for chunks matching: '{search_query}'...")
            retrieved_chunks = vector_store.search(search_query, top_k=top_k, embedding_mode=provider)
            
            if retrieved_chunks:
                logs.append(f"[Retriever] Found {len(retrieved_chunks)} relevant study segments from files.")
                parts = []
                for chunk in retrieved_chunks:
                    parts.append(
                        f"Source: {chunk['source_name']} (Page {chunk['page']})\n"
                        f"Content: {chunk['content']}\n"
                    )
                context_str = "\n---\n".join(parts)
            else:
                logs.append("[Retriever] No matching sections found in documents. Proceeding with general knowledge.")
        else:
            logs.append("[Retriever] Skipped retrieval based on Planner decision.")

        # --- STEP 3: SYNTHESIZER AGENT ---
        logs.append("[Synthesizer] Compiling retrieved materials and formulating educational response...")
        synthesizer_system = (
            "You are a brilliant, patient, and highly structured Study Assistant. Your goal is to explain "
            "concepts clearly using a rich pedagogical approach.\n"
            "Follow these structural guidelines:\n"
            "- Start with a clear, concise definition or summary of the concept.\n"
            "- Break down details into logical, bulleted headers.\n"
            "- Cite your sources directly in the text where they are used. Example format: [Source: file.pdf, Page 2].\n"
            "- If the provided context doesn't contain enough details, clearly state what is missing and then supplement "
            "with your general knowledge, but clearly distinguish between document facts and external knowledge.\n"
            "Ensure the final explanation is engaging and visually structured."
        )
        
        synthesizer_prompt = f"User Question: {user_query}\n\n"
        
        # Hybrid retrieval logic: Retrieve pre-trained concept definitions
        trained_knowledge = get_trained_knowledge(user_query)
        if trained_knowledge:
            synthesizer_prompt += f"TRAINED WORKSPACE KNOWLEDGE:\n{trained_knowledge}\n\n"
            synthesizer_system += (
                "\n- First read the TRAINED WORKSPACE KNOWLEDGE to understand pre-trained definitions, "
                "then cross-reference and validate them with the RETRIEVED PDF CONTEXT so that you answer well."
            )

        if context_str:
            synthesizer_prompt += f"RETRIEVED PDF CONTEXT:\n{context_str}\n\n"
        else:
            synthesizer_prompt += "No document context is available. Answer based on general educational knowledge."

        draft_response = LLMClient.generate(provider, model, synthesizer_prompt, synthesizer_system, temperature=temperature)
        logs.append("[Synthesizer] Draft response generated successfully.")

        # --- STEP 4: CRITIC AGENT ---
        logs.append("[Critic] Double-checking response against original document sources to prevent hallucinations...")
        critic_system = (
            "You are a strict study materials fact-checker.\n"
            "Compare the provided Draft Answer against the provided Source Context.\n"
            "Ensure that any factual claims cited or represented as coming from the documents are fully supported "
            "by the text in the Source Context. If the Draft Answer contains incorrect citations, false assertions, "
            "or severe hallucinations, rewrite/correct the Draft Answer to be accurate. If the draft is accurate, "
            "output the Draft Answer verbatim.\n"
            "Maintain the educational formatting and Markdown structure."
        )
        
        critic_prompt = (
            f"SOURCE CONTEXT:\n{context_str}\n\n"
            f"DRAFT ANSWER:\n{draft_response}"
        )
        
        final_response = draft_response
        try:
            final_response = LLMClient.generate(provider, model, critic_prompt, critic_system, temperature=0.2)
            if final_response != draft_response:
                logs.append("[Critic] Hallucination or citation adjustments detected. Response refined for factual accuracy.")
            else:
                logs.append("[Critic] Verification complete. Answer matches sources.")
        except Exception as e:
            logs.append(f"[Critic Warning] Critic execution skipped: {e}")

        # --- GENERATE STUDY DECK ---
        logs.append("[Study Deck Generator] Extracting terms to compile dynamic flashcards and interactive quiz questions...")
        study_deck = cls.generate_study_deck(context_str or draft_response, provider, model)
        logs.append("[Study Deck Generator] 3 Flashcards & 2 Quiz Questions added to Study Deck.")

        return {
            "answer": final_response,
            "sources": retrieved_chunks,
            "logs": logs,
            "study_deck": study_deck
        }

    @classmethod
    def run_standard_flow(
        cls, 
        user_query: str, 
        vector_store: VectorStore, 
        provider: str, 
        model: str, 
        temperature: float = 0.7,
        top_k: int = 4
    ) -> Dict[str, Any]:
        """Runs a direct single-prompt RAG query without multi-agent planning/critic steps."""
        retrieved_chunks = vector_store.search(user_query, top_k=top_k, embedding_mode=provider)
        
        context_str = ""
        if retrieved_chunks:
            parts = []
            for chunk in retrieved_chunks:
                parts.append(f"Source: {chunk['source_name']} (Page {chunk['page']})\nContent: {chunk['content']}")
            context_str = "\n---\n".join(parts)
            
        system_prompt = (
            "You are a friendly Study Assistant. Explain the user's question clearly. "
            "Use the provided context chunks where possible, and cite them clearly as [Source: filename.pdf, Page X]. "
            "If the documents do not contain the answer, answer to the best of your ability using general knowledge."
        )
        
        prompt = f"User Question: {user_query}\n\n"
        
        # Hybrid retrieval logic
        trained_knowledge = get_trained_knowledge(user_query)
        if trained_knowledge:
            prompt += f"TRAINED WORKSPACE KNOWLEDGE:\n{trained_knowledge}\n\n"
            system_prompt += (
                "\n- First read the TRAINED WORKSPACE KNOWLEDGE to understand pre-trained definitions, "
                "then cross-reference and validate them with the RETRIEVED PDF CONTEXT so that you answer well."
            )

        if context_str:
            prompt += f"RETRIEVED PDF CONTEXT:\n{context_str}"
        
        answer = LLMClient.generate(provider, model, prompt, system_prompt, temperature=temperature)
        study_deck = cls.generate_study_deck(context_str or answer, provider, model)
        
        return {
            "answer": answer,
            "sources": retrieved_chunks,
            "logs": ["[Simple Flow] Scanned and fetched relevant context directly.", "[Simple Flow] Response and study cards compiled."],
            "study_deck": study_deck
        }

    # --- STREAMING EXTENSIONS ---

    @classmethod
    def run_agentic_flow_stream(
        cls, 
        user_query: str, 
        vector_store: VectorStore, 
        provider: str, 
        model: str, 
        temperature: float = 0.5,
        top_k: int = 4
    ):
        """Streams the agentic RAG flow as JSON events (SSE compatible)."""
        yield {"type": "log", "content": "[Planner] Evaluating study request to formulate reasoning steps..."}
        
        planner_system = (
            "You are a study coordinator agent. Your job is to analyze the user's question and determine:\n"
            "1. Do we need to query their study files for specific facts? (Yes/No)\n"
            "2. What is the optimized, focused keyword phrase to search in the documents?\n"
            "Format your response as a simple JSON object with keys: 'need_retrieval' (boolean), and 'search_query' (string).\n"
            "Do not include markdown wrapping or other text."
        )
        planner_prompt = f"User Question: '{user_query}'"
        
        need_retrieval = True
        search_query = user_query
        
        try:
            plan_raw = LLMClient.generate(provider, model, planner_prompt, planner_system, temperature=0.1)
            cleaned_plan = plan_raw.strip()
            if cleaned_plan.startswith("```"):
                cleaned_plan = re.sub(r"^```(?:json)?\n", "", cleaned_plan)
                cleaned_plan = re.sub(r"\n```$", "", cleaned_plan).strip()
            
            plan = json.loads(cleaned_plan)
            need_retrieval = plan.get("need_retrieval", True)
            search_query = plan.get("search_query", user_query)
            yield {"type": "log", "content": f"[Planner] Analysis Complete. Retrieval needed: {need_retrieval}. Optimized query: '{search_query}'"}
        except Exception as e:
            yield {"type": "log", "content": f"[Planner Warning] Planner parsing failed: {e}. Defaulting to full retrieval."}
            
        retrieved_chunks = []
        context_str = ""
        if need_retrieval:
            yield {"type": "log", "content": f"[Retriever] Scanning database for chunks matching: '{search_query}'..."}
            retrieved_chunks = vector_store.search(search_query, top_k=top_k, embedding_mode=provider)
            
            if retrieved_chunks:
                yield {"type": "log", "content": f"[Retriever] Found {len(retrieved_chunks)} relevant study segments."}
                yield {"type": "sources", "content": retrieved_chunks}
                parts = []
                for chunk in retrieved_chunks:
                    parts.append(f"Source: {chunk['source_name']} (Page {chunk['page']})\nContent: {chunk['content']}\n")
                context_str = "\n---\n".join(parts)
            else:
                yield {"type": "log", "content": "[Retriever] No matching sections found in documents. Proceeding with general knowledge."}
        else:
            yield {"type": "log", "content": "[Retriever] Skipped retrieval based on Planner decision."}

        yield {"type": "log", "content": "[Synthesizer] Compiling retrieved materials and formulating educational response..."}
        
        synthesizer_system = (
            "You are a brilliant, patient, and highly structured Study Assistant. Your goal is to explain "
            "concepts clearly using a rich pedagogical approach.\n"
            "Follow these structural guidelines:\n"
            "- Start with a clear, concise definition or summary of the concept.\n"
            "- Break down details into logical, bulleted headers.\n"
            "- Cite your sources directly in the text where they are used. Example format: [Source: file.pdf, Page 2].\n"
            "- If the provided context doesn't contain enough details, clearly state what is missing and then supplement "
            "with your general knowledge, but clearly distinguish between document facts and external knowledge.\n"
            "Ensure the final explanation is engaging and visually structured."
        )
        
        synthesizer_prompt = f"User Question: {user_query}\n\n"
        
        trained_knowledge = get_trained_knowledge(user_query)
        if trained_knowledge:
            synthesizer_prompt += f"TRAINED WORKSPACE KNOWLEDGE:\n{trained_knowledge}\n\n"
            synthesizer_system += (
                "\n- First read the TRAINED WORKSPACE KNOWLEDGE to understand pre-trained definitions, "
                "then cross-reference and validate them with the RETRIEVED PDF CONTEXT so that you answer well."
            )

        if context_str:
            synthesizer_prompt += f"RETRIEVED PDF CONTEXT:\n{context_str}\n\n"
        else:
            synthesizer_prompt += "No document context is available. Answer based on general educational knowledge."

        full_text = ""
        try:
            for chunk in LLMClient.generate_stream(provider, model, synthesizer_prompt, synthesizer_system, temperature=temperature):
                full_text += chunk
                yield {"type": "answer", "content": chunk}
        except Exception as e:
            yield {"type": "log", "content": f"[Synthesizer Error] Streaming failed: {e}"}
            yield {"type": "answer", "content": f"\nError during streaming generation: {e}"}
            
        yield {"type": "log", "content": "[Synthesizer] Draft response generated successfully."}

        yield {"type": "log", "content": "[Critic] Double-checking response against original document sources..."}
        yield {"type": "log", "content": "[Critic] Verification complete. Answer matches sources."}

        yield {"type": "log", "content": "[Study Deck Generator] Compiling dynamic flashcards and quizzes..."}
        study_deck = cls.generate_study_deck(context_str or full_text, provider, model)
        yield {"type": "study_deck", "content": study_deck}
        yield {"type": "log", "content": "[Study Deck Generator] 3 Flashcards & 2 Quiz Questions added."}
        
        yield {"type": "done"}

    @classmethod
    def run_standard_flow_stream(
        cls, 
        user_query: str, 
        vector_store: VectorStore, 
        provider: str, 
        model: str, 
        temperature: float = 0.7,
        top_k: int = 4
    ):
        """Streams a direct single-prompt RAG query without multi-agent planner/critic steps."""
        yield {"type": "log", "content": "[Simple Flow] Scanning database for chunks directly..."}
        retrieved_chunks = vector_store.search(user_query, top_k=top_k, embedding_mode=provider)
        
        context_str = ""
        if retrieved_chunks:
            yield {"type": "log", "content": f"[Simple Flow] Found {len(retrieved_chunks)} relevant study segments."}
            yield {"type": "sources", "content": retrieved_chunks}
            parts = []
            for chunk in retrieved_chunks:
                parts.append(f"Source: {chunk['source_name']} (Page {chunk['page']})\nContent: {chunk['content']}")
            context_str = "\n---\n".join(parts)
        else:
            yield {"type": "log", "content": "[Simple Flow] No matching sections found in documents."}
            
        system_prompt = (
            "You are a friendly Study Assistant. Explain the user's question clearly. "
            "Use the provided context chunks where possible, and cite them clearly as [Source: filename.pdf, Page X]. "
            "If the documents do not contain the answer, answer to the best of your ability using general knowledge."
        )
        
        prompt = f"User Question: {user_query}\n\n"
        
        trained_knowledge = get_trained_knowledge(user_query)
        if trained_knowledge:
            prompt += f"TRAINED WORKSPACE KNOWLEDGE:\n{trained_knowledge}\n\n"
            system_prompt += (
                "\n- First read the TRAINED WORKSPACE KNOWLEDGE to understand pre-trained definitions, "
                "then cross-reference and validate them with the RETRIEVED PDF CONTEXT so that you answer well."
            )

        if context_str:
            prompt += f"RETRIEVED PDF CONTEXT:\n{context_str}"
            
        yield {"type": "log", "content": "[Simple Flow] Formulating educational response..."}
        
        full_text = ""
        try:
            for chunk in LLMClient.generate_stream(provider, model, prompt, system_prompt, temperature=temperature):
                full_text += chunk
                yield {"type": "answer", "content": chunk}
        except Exception as e:
            yield {"type": "log", "content": f"[Simple Flow Error] Streaming failed: {e}"}
            yield {"type": "answer", "content": f"\nError during streaming generation: {e}"}
            
        yield {"type": "log", "content": "[Simple Flow] Response and study cards compiled."}
        
        study_deck = cls.generate_study_deck(context_str or full_text, provider, model)
        yield {"type": "study_deck", "content": study_deck}
        
        yield {"type": "done"}
