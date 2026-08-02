// AUTOMĀTISKAIS TRĪS VALODU DZINĒJS — AVISE PLATFORM ENGINE
function changeLanguage(lang) {
    // Pārbauda, vai tulkojumu dati ir ielādējušies
    if (typeof translations === 'undefined') return;
    
    const data = translations[lang];
    if (!data) return;

    // Tekstu elementu sasaiste
    document.getElementById('txt-sub').textContent = data.sub;
    document.getElementById('m-prod').textContent = data.mProd;
    document.getElementById('m-tech').textContent = data.mTech;
    document.getElementById('m-co2').textContent = data.mCo2;
    document.getElementById('m-inv').textContent = data.mInv;
    document.getElementById('m-cont').textContent = data.mCont;
    document.getElementById('h-prod').textContent = data.hProd;
    document.getElementById('p-prod').textContent = data.pProd;
    document.getElementById('h-feed').textContent = data.hFeed;
    document.getElementById('p-feed').textContent = data.pFeed;
    document.getElementById('h-man').textContent = data.hMan;
    document.getElementById('p-man').textContent = data.pMan;
    document.getElementById('h-tech').textContent = data.hTech;
    document.getElementById('p-tech').textContent = data.pTech;
    document.getElementById('h-co2-title').textContent = data.hCo2;
    document.getElementById('p-co2').textContent = data.pCo2;
    document.getElementById('h-inv-title').textContent = data.hInv;
    document.getElementById('p-inv-desc').textContent = data.pInv;
    
    // Personas dati
    document.getElementById('c-name').textContent = data.cName;
    document.getElementById('c-role').textContent = data.cRole;
    document.getElementById('c-lbl-email').textContent = data.cLblEmail;

    // Rekvizītu dati
    document.getElementById('h-cont-title').textContent = data.hCont;
    document.getElementById('p-cont-desc').textContent = data.pCont;
    document.getElementById('lbl-reg').textContent = data.lblReg;
    document.getElementById('lbl-addr').textContent = data.lblAddr;
    document.getElementById('lbl-phone').textContent = data.lblPhone;
    document.getElementById('c-corp-name').textContent = data.corpName;
    document.getElementById('c-reg-val').textContent = data.regVal;
    document.getElementById('c-addr-val').textContent = data.addrVal;
    document.getElementById('c-phone-val').textContent = data.phoneVal;

    // Pogas izgaismošanas pārslēgšana
    document.getElementById('lang-en').classList.remove('active');
    document.getElementById('lang-lv').classList.remove('active');
    document.getElementById('lang-lt').classList.remove('active');
    
    const activeBtn = document.getElementById('lang-' + lang);
    if (activeBtn) activeBtn.classList.add('active');
}

// Palaidēji pēc lapas ielādes
document.addEventListener("DOMContentLoaded", () => { 
    changeLanguage('en'); 
    
    document.getElementById('lang-en').addEventListener('click', (e) => { e.preventDefault(); changeLanguage('en'); });
    document.getElementById('lang-lv').addEventListener('click', (e) => { e.preventDefault(); changeLanguage('lv'); });
    document.getElementById('lang-lt').addEventListener('click', (e) => { e.preventDefault(); changeLanguage('lt'); });
});