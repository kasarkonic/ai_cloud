function changeLanguage(lang) {
    if (typeof translations === 'undefined') return;
    const data = translations[lang];
    if (!data) return;

    // Navigācijas punkti
    document.getElementById('txt-sub').textContent = data.sub;
    document.getElementById('m-home').textContent = data.mHome;
    document.getElementById('m-feed').textContent = data.mFeed;
    document.getElementById('m-tech').textContent = data.mTech;
    document.getElementById('m-renew').textContent = data.mRenew;
    document.getElementById('m-reg').textContent = data.mReg;
    document.getElementById('m-co2').textContent = data.mCo2;
    document.getElementById('m-part').textContent = data.mPart;
    document.getElementById('m-cont').textContent = data.mCont;
    
    // 01 / HOME sasaiste
    document.getElementById('h-home').textContent = data.hHome;
    document.getElementById('p-home-1').textContent = data.pHome1;
    document.getElementById('p-home-2').textContent = data.pHome2;
    document.getElementById('p-home-3').textContent = data.pHome3;
    document.getElementById('p-home-4').textContent = data.pHome4;
    document.getElementById('p-home-5').textContent = data.pHome5;

    // 02 / FEEDSTOCK sasaiste
    document.getElementById('h-feed').textContent = data.hFeed;
    document.getElementById('p-feed-1').textContent = data.pFeed1;
    document.getElementById('p-feed-2').textContent = data.pFeed2;
    document.getElementById('p-feed-3').textContent = data.pFeed3;

    // 03 / TECHNOLOGY sasaiste
    document.getElementById('h-tech').textContent = data.hTech;
    document.getElementById('p-tech-1').textContent = data.pTech1;
    document.getElementById('p-tech-2').textContent = data.pTech2;
    document.getElementById('p-tech-3').textContent = data.pTech3;

    // 04 / RENEWABLE sasaiste
    document.getElementById('h-renew').textContent = data.hRenew;
    document.getElementById('p-renew-1').textContent = data.pRenew1;
    document.getElementById('p-renew-2').textContent = data.pRenew2;

    // 05 / REGIONAL sasaiste
    document.getElementById('h-reg').textContent = data.hReg;
    document.getElementById('p-reg-1').textContent = data.pReg1;

    // 06 / CO2 sasaiste
    document.getElementById('h-co2-title').textContent = data.hCo2;
    document.getElementById('p-co2-1').textContent = data.pCo21;

    // 07 / PARTNERSHIPS sasaiste
    document.getElementById('h-part-title').textContent = data.hPart;
    document.getElementById('p-part-1').textContent = data.pPart1;
    document.getElementById('p-part-2').textContent = data.pPart2;
    document.getElementById('p-part-3').textContent = data.pPart3;

    // 08 / CONTACT sasaiste
    document.getElementById('h-cont-title').textContent = data.hCont;
    document.getElementById('p-cont-desc').textContent = data.pCont;
    document.getElementById('lbl-direct').textContent = data.lblDirect;
    document.getElementById('lbl-latvia').textContent = data.lblLatvia;
    document.getElementById('lbl-lithuania').textContent = data.lblLithuania;

    // Aktīvo pogu izgaismošana trim valodām
    document.getElementById('lang-en').classList.remove('active');
    document.getElementById('lang-lv').classList.remove('active');
    document.getElementById('lang-lt').classList.remove('active');
    const activeBtn = document.getElementById('lang-' + lang);
    if (activeBtn) activeBtn.classList.add('active');
}

document.addEventListener("DOMContentLoaded", () => { 
    changeLanguage('en'); 
    document.getElementById('lang-en').addEventListener('click', (e) => { e.preventDefault(); changeLanguage('en'); });
    document.getElementById('lang-lv').addEventListener('click', (e) => { e.preventDefault(); changeLanguage('lv'); });
    document.getElementById('lang-lt').addEventListener('click', (e) => { e.preventDefault(); changeLanguage('lt'); });
});